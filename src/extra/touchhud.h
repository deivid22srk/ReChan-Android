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
    Hidden = 0,   // cutscenes/NIS, intros, loading, credits, non-interactive
    OnFoot = 1,   // normal gameplay (combat included)
    Climbing = 2, // ladders / ledges / poles: reduced control set
    Menu = 3,     // title/FE menus, asset setup, pause: d-pad style navigation
};

// Called once per frame from the main loop (game thread).
void PublishFrame();

// Current context (also readable from any thread).
HudContext GetContext();

// Consume a pending menu tap (screen pixel position) published by the touch
// input pump. Used by feCustomMenuMgr for direct entry hit-testing.
bool ConsumeMenuTap(float& x, float& y);

} // namespace touchhud

#endif // RC_PLATFORM_ANDROID
