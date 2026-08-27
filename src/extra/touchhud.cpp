// touchhud.cpp — publishes the gameplay context for the Android touch overlay.
// Signals used (all already exposed by the engine):
//   - g_game->GetState()            -> only GameState::Play is interactive
//   - g_feCustomMenuMgr->IsActive() -> custom menu overlays gameplay
//   - g_directorActive / g_director->scriptState -> cutscenes (NIS)
//   - Player::s_player->actionState -> ladder/ledge/pole states
// Touch input returns via androidbridge (same path as a physical gamepad).
#include "extra/touchhud.h"

#if defined(RC_PLATFORM_ANDROID)

#include <jni.h>

#include "gen/game.h"
#include "gen/common.h"
#include "gen/director.h"
#include "ai/player.h"
#include "ai/humanoid.h"
#include "extra/fecustommenumgr.h"
#include "extra/touchcontrols.h"
#include "pddi/gles/AndroidPlatform.h"

namespace touchhud {

namespace {
HudContext s_context = HudContext::Hidden;
s32 s_startPulseFrames = 0;   // tap->Start pulse (title "press start")
s32 s_confirmPulseFrames = 0; // tap->A pulse ("press A to continue" screens)

bool IsClimbState(s32 actionState) {
    switch (actionState) {
        case AS_POLE_IDLE:          // 18
        case AS_LEDGE_LATCH:        // 23
        case AS_LEDGE_PULLUP:       // 24
        case AS_LADDER_CLIMB_DOWN:  // 25
        case AS_LADDER_CLIMB_UP:    // 26
        case AS_LADDER_CLIMBING:    // 27
        case AS_LADDER_DISMOUNT:    // 28
            return true;
        default:
            return false;
    }
}

HudContext ComputeContext() {
    // Custom menu overlays (asset setup, pause, title/FE menus): the generic
    // list pages are fully touch-operable (direct entry taps + "< value >"
    // steppers), so the on-screen pad there is redundant clutter - hide it.
    // Only the pages that cannot be driven by taps (scrolling lists, slot
    // tables, key-binding grid) keep the d-pad navigation set.
    if (g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive()) {
        return g_feCustomMenuMgr->NeedsVirtualPadNavigation()
            ? HudContext::Menu
            : HudContext::Hidden;
    }

    if (!g_game) {
        return HudContext::Hidden;
    }

    switch (g_game->GetState()) {
        // These states are only reached WITHOUT an active custom menu in
        // non-interactive moments (transitions or "press a button" screens):
        //   - Title/TitleLoop: press-start phase - a tap anywhere pulses
        //     Start (PublishFrame), so no visible pad is needed;
        //   - OpenFE/FE/Menu/LocationMenu/OpenLocationMenu: 1-frame
        //     transitions or frames where the menu is not up yet;
        //   - DbgMenu: empty stub, nothing interactive;
        //   - EndGameLoop: Game Over "press A to continue" - tap pulses A;
        //   - EndLevelLoop: Level Results tally - tap pulses A (fast-forward
        //     during the count, confirm after).
        case GameState::Title:
        case GameState::TitleLoop:
        case GameState::OpenFE:
        case GameState::FE:
        case GameState::Menu:
        case GameState::DbgMenu:
        case GameState::LocationMenu:
        case GameState::OpenLocationMenu:
        case GameState::EndGameLoop:
        case GameState::EndLevelLoop:
            return HudContext::Hidden;

        case GameState::Intro:
            // Legal splash auto-expires, but a tap can skip it (see the
            // confirm pulse in PublishFrame) - no HUD needed while it plays.
            return HudContext::Hidden;

        case GameState::Play: {
            if (g_directorActive != 0 || (g_director && g_director->scriptState != 0)) {
                // Cutscene / NIS running.
                return HudContext::Hidden;
            }
            if (const Player* player = Player::s_player) {
                if (IsClimbState(player->actionState)) {
                    return HudContext::Climbing;
                }
            }
            return HudContext::OnFoot;
        }

        default:
            // Intros, movies, loading, credits, error loops: touch is useless.
            return HudContext::Hidden;
    }
}

} // namespace

void ProcessInput() {
    // On-screen touch controls (joystick + action buttons): hit-test the
    // current fingers and post virtual gamepad buttons/axes. Runs BEFORE
    // PlatformInput::ServiceInput() in the main loop (see main.cpp) so the
    // state lands in this frame's pad snapshot, not the next one.
    touchcontrols::Update();
}

void PublishFrame() {
    s_context = ComputeContext();
    androidbridge::SetHudContext(static_cast<u32>(s_context));

    // Touch diagnostics (throttled): confirm the touch->mouse chain end to end.
    static s32 s_diagCounter = 0;
    if ((s_diagCounter++ & 0x3F) == 0) {
        float tx = 0.0f, ty = 0.0f;
        const bool touchDown = androidbridge::GetTouchMouse(&tx, &ty);
        const bool menuActive = g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive();
        const bool mouseActive = g_feCustomMenuMgr ? g_feCustomMenuMgr->IsMouseActive() : false;
        LOG("[TouchDiag] down=%d pos=(%.0f,%.0f) menuActive=%d mouseActive=%d"
            " hadGp=%d hadKb=%d ctx=%u physPad=%d",
            touchDown ? 1 : 0, tx, ty, menuActive ? 1 : 0, mouseActive ? 1 : 0,
            (g_actionInput && g_actionInput->HadGamepadInputThisFrame()) ? 1 : 0,
            (g_actionInput && g_actionInput->HadKeyboardInputThisFrame()) ? 1 : 0,
            static_cast<u32>(s_context),
            androidbridge::IsPhysicalPadConnected() ? 1 : 0);
    }

    // "Press a button" screens (no menu active): a tap anywhere becomes the
    // button that screen waits for, so touch-only players are never stuck:
    //   - TitleLoop          : Start ("press start" screen)
    //   - EndGameLoop        : A     (Game Over "press A to continue"; the
    //                                  screen also accepts Start/Cross via the
    //                                  virtual pad, and the fade phase ignores
    //                                  input, hence the gameOverFadeType gate)
    //   - Intro legal splash : A     (AnyJustPressed skips the splash; on PC
    //                                  any mouse button does the same)
    //   - EndLevelLoop       : A     (Level Results tally: during the count
    //                                  A = fast-forward - same as holding A on
    //                                  a real pad; once "Press [A] to continue"
    //                                  shows, A confirms and leaves the screen)
    // Two frames so the pad edge is sampled reliably. Suspended while a
    // physical gamepad is connected: the real pad's own Start/A must drive
    // these screens, and a stray screen tap must not.
    if (androidbridge::ConsumeTouchTap()) {
        const bool menuActive = g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive();
        const GameState st = g_game ? g_game->GetState() : GameState::Null;
        if (!menuActive && !androidbridge::IsPhysicalPadConnected()) {
            if (st == GameState::TitleLoop && s_startPulseFrames == 0) {
                s_startPulseFrames = 2;
            }
            else if (st == GameState::EndGameLoop
                     && g_game && !g_game->IsGameOverFading()
                     && s_confirmPulseFrames == 0) {
                s_confirmPulseFrames = 2;
            }
            else if (st == GameState::Intro
                     && g_game && g_game->IsIntroSplashActive()
                     && s_confirmPulseFrames == 0) {
                s_confirmPulseFrames = 2;
            }
            else if (st == GameState::EndLevelLoop && s_confirmPulseFrames == 0) {
                s_confirmPulseFrames = 2;
            }
        }
    }
    if (s_startPulseFrames > 0) {
        // The gamepad state is only polled by PlatformInput when connected is
        // set; a touch-only device has no physical pad, so mark the virtual
        // pad connected for the duration of the pulse.
        androidbridge::PostGamepadConnected(true);
        androidbridge::PostGamepadButton(GamepadButton::Start, s_startPulseFrames == 2);
        --s_startPulseFrames;
    }
    if (s_confirmPulseFrames > 0) {
        androidbridge::PostGamepadConnected(true);
        androidbridge::PostGamepadButton(GamepadButton::A, s_confirmPulseFrames == 2);
        --s_confirmPulseFrames;
    }

    // With no menu active, nothing during game.Step() consumed the pending
    // UP-side tap (the one carrying a position). Drain it so it can't
    // resurface as a phantom click when the next menu opens - e.g. taps on
    // the Game Over screen must not click an entry of the Location menu that
    // appears right after the fade, and HUD button taps during gameplay must
    // not click the pause menu opened a moment later.
    //
    // NOTE: direct menu TAPS stay enabled even with a physical gamepad
    // connected - mixed input is convenient and harmless - so this drain only
    // runs while no menu is up; an active menu's Invoke() consumes its taps
    // itself during game.Step().
    if (!(g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive())) {
        float drainX = 0.0f;
        float drainY = 0.0f;
        ConsumeMenuTap(drainX, drainY);
    }
}

HudContext GetContext() {
    return s_context;
}

bool ConsumeMenuTap(float& x, float& y) {
    return androidbridge::ConsumeTouchTapPos(&x, &y);
}

} // namespace touchhud

// --- JNI surface for the Java touch overlay (com.deivid22srk.rechan.hud) ---
// Touch controls feed the same androidbridge gamepad state a physical pad
// uses, so the engine cannot tell them apart and no duplicate input path
// exists.

extern "C" {

JNIEXPORT void JNICALL
Java_com_deivid22srk_rechan_hud_HudBridge_nativePostButton(
    JNIEnv*, jclass, jint button, jboolean down) {
    androidbridge::PostGamepadButton(button, down == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_deivid22srk_rechan_hud_HudBridge_nativePostAxis(
    JNIEnv*, jclass, jint axis, jfloat value) {
    androidbridge::PostGamepadAxis(axis, value);
}

JNIEXPORT void JNICALL
Java_com_deivid22srk_rechan_hud_HudBridge_nativePostConnected(
    JNIEnv*, jclass, jboolean connected) {
    androidbridge::PostGamepadConnected(connected == JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_com_deivid22srk_rechan_hud_HudBridge_nativePollContext(JNIEnv*, jclass) {
    return static_cast<jint>(touchhud::GetContext());
}

JNIEXPORT void JNICALL
Java_com_deivid22srk_rechan_hud_HudBridge_nativeSetPhysicalGamepad(
    JNIEnv*, jclass, jboolean connected) {
    // Pushed by GameActivity's InputDevice.InputDeviceListener: a physical
    // (Bluetooth/USB) gamepad connecting hides the on-screen touch HUD, the
    // last pad disconnecting brings it back.
    androidbridge::SetPhysicalPadConnected(connected == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_deivid22srk_rechan_hud_HudBridge_nativeSetGhostPadDeviceIds(
    JNIEnv* env, jclass, jintArray ids) {
    // Device ids of "ghost" pads (Xiaomi uinput fingerprint readers that
    // report SOURCE_GAMEPAD|SOURCE_JOYSTICK while isVirtual() is false).
    // AndroidInput.cpp swallows their events so they can neither confirm
    // physical-pad presence nor inject phantom buttons/axes.
    if (env == nullptr || ids == nullptr) {
        androidbridge::SetGhostPadDeviceIds(nullptr, 0);
        return;
    }
    const jsize n = env->GetArrayLength(ids);
    if (n <= 0) {
        androidbridge::SetGhostPadDeviceIds(nullptr, 0);
        return;
    }
    jint buf[16];
    const jsize count = n < 16 ? n : 16;
    env->GetIntArrayRegion(ids, 0, count, buf);
    androidbridge::SetGhostPadDeviceIds(buf, count);
}

} // extern "C"

#endif // RC_PLATFORM_ANDROID
