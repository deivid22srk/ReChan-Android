// touchhud.h — Contextual HUD contract (Android only).
//
// The game loop publishes a coarse "context" every frame; the Java overlay
// (com.deivid22srk.rechan.hud) polls it via JNI and adapts the on-screen
// touch controls. Touch input flows back through androidbridge's gamepad
// state, so the engine sees the virtual pad exactly like a physical one.
#pragma once

#include "core.h"

#if defined(RC_PLATFORM_ANDROID)

namespace touchhud {

// Keep in sync with HudBridge.CONTEXT_* in the Java layer.
enum class HudContext : u32 {
    Hidden = 0,   // menus, cutscenes/NIS, intros, loading, non-play states
    OnFoot = 1,   // normal gameplay (combat included)
    Climbing = 2, // ladders / ledges / poles: reduced control set
};

// Called once per frame from the main loop (game thread).
void PublishFrame();

// Current context (also readable from any thread).
HudContext GetContext();

} // namespace touchhud

#endif // RC_PLATFORM_ANDROID
