// touchcontrols.h — on-screen touch controls rendered by the engine itself
// (Android only). Virtual joystick + action buttons feed the shared
// androidbridge gamepad state, exactly like a physical gamepad. No Java
// overlay involved: input is multi-touch from the NativeActivity input queue
// and rendering uses the engine's own 2D overlay (ScreenDraw).
#pragma once

#include "core.h"

#if defined(RC_PLATFORM_ANDROID)

namespace touchcontrols {

// Process the current multi-touch state against the visible controls and
// publish virtual gamepad state. Called once per game-loop frame.
void Update();

// Render the gameplay touch controls overlay. Called from Display::EndFrame
// just before SwapBuffers so it draws on top of everything.
void Render();

// Reset every held virtual button/axis (context switch, pause, gamepad plug).
void Reset();

// --- Cutscene / movie skip -------------------------------------------------

// Gate the skip button with PlayMovie's skippable flag: non-skippable movies
// (logos, credits) must neither show the button nor post virtual input.
void SetMovieSkippable(bool skippable);

// Release the virtual Start press and clear the skip state. MUST be called
// after the PlayMovie loop ends (skip or natural end), otherwise Start stays
// held down and leaks into the next game state.
void EndMovieSkip();

// Process touches against the "skip" button (used inside the blocking
// PlayMovie loop where the main loop doesn't run).
void UpdateMovieSkip();

// Render the skip button (same context as UpdateMovieSkip).
void RenderMovieSkip();

} // namespace touchcontrols

#endif // RC_PLATFORM_ANDROID
