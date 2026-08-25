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
#include "pddi/gles/AndroidPlatform.h"

namespace touchhud {

namespace {
HudContext s_context = HudContext::Hidden;
s32 s_startPulseFrames = 0;

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
    // Custom menu overlays (asset setup, pause, title/FE menus) always win:
    // they need d-pad style navigation, available with or without a gamepad.
    if (g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive()) {
        return HudContext::Menu;
    }

    if (!g_game) {
        return HudContext::Hidden;
    }

    switch (g_game->GetState()) {
        case GameState::Title:
        case GameState::TitleLoop:
        case GameState::OpenFE:
        case GameState::FE:
        case GameState::Menu:
        case GameState::DbgMenu:
        case GameState::LocationMenu:
        case GameState::OpenLocationMenu:
            return HudContext::Menu;

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
            " hadGp=%d hadKb=%d ctx=%u",
            touchDown ? 1 : 0, tx, ty, menuActive ? 1 : 0, mouseActive ? 1 : 0,
            (g_actionInput && g_actionInput->HadGamepadInputThisFrame()) ? 1 : 0,
            (g_actionInput && g_actionInput->HadKeyboardInputThisFrame()) ? 1 : 0,
            static_cast<u32>(s_context));
    }

    // Title screen "press any button": a tap anywhere becomes a Start press
    // (the custom menu is inactive during that phase, so taps aren't menu
    // clicks yet). Two frames so the pad edge is sampled reliably.
    if (androidbridge::ConsumeTouchTap()) {
        const bool menuActive = g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive();
        const bool onTitle = g_game && g_game->GetState() == GameState::TitleLoop;
        if (!menuActive && onTitle && s_startPulseFrames == 0) {
            s_startPulseFrames = 2;
        }
    }
    if (s_startPulseFrames > 0) {
        androidbridge::PostGamepadButton(GamepadButton::Start, s_startPulseFrames == 2);
        --s_startPulseFrames;
    }
}

HudContext GetContext() {
    return s_context;
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

} // extern "C"

#endif // RC_PLATFORM_ANDROID
