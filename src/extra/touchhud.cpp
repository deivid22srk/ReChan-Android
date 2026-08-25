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
    if (!g_game || g_game->GetState() != GameState::Play) {
        return HudContext::Hidden;
    }
    if (g_feCustomMenuMgr && g_feCustomMenuMgr->IsActive()) {
        return HudContext::Hidden;
    }
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

} // namespace

void PublishFrame() {
    s_context = ComputeContext();
    androidbridge::SetHudContext(static_cast<u32>(s_context));
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
