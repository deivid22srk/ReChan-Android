// AndroidPlatform.h — bridge between the NativeActivity shell and the GLES
// backend. The shell (UI/input thread) feeds window lifecycle events and raw
// gamepad state in here; glesDisplay/glesGamepad consume them on the render
// thread. Everything is lock-free via atomics: producers are single-threaded
// (the input queue pump) and consumers run on the game thread only.
#pragma once

#include <atomic>
#include <cstdint>

namespace androidbridge {

using NativeWindowHandle = void*; // ANativeWindow*

// --- Window lifecycle (shell thread -> game thread) ---

// Publish a new ANativeWindow (already acquired by the caller). Passing
// nullptr marks surface loss (APP_CMD_TERM_WINDOW).
void SetNativeWindow(NativeWindowHandle window);

// Request engine shutdown (APP_CMD_DESTROY / destroyRequested).
void RequestExit();

// Game thread: take the pending native window, if any.
NativeWindowHandle TakePendingWindow();

// Game thread: current published window (may be null between pause/resume).
NativeWindowHandle PeekCurrentWindow();

// Monotonic counter bumped on every window transition (publish or clear).
// The display compares it against the generation its EGL surface was created
// from to detect stale surfaces after fast TERM->INIT sequences.
uint64_t WindowGeneration();

// Game thread: true once exit has been requested.
bool ExitRequested();

// --- Gamepad state (input thread -> game thread) ---
// Button indices/axes use the pddi GLFW-style layout:
// sticks -1..1 with Y up-positive, triggers -1..1 (-1 = released).

void PostGamepadButton(int pddiButton, bool down);
void PostGamepadAxis(int pddiAxis, float value);
void PostGamepadConnected(bool connected);

// Raw shared state snapshot for glesGamepad::Poll().
struct PadSnapshot {
    uint32_t heldButtons = 0;
    float axes[6] = {};
    bool connected = false;
};

PadSnapshot LoadPadSnapshot();

// --- Touch HUD context (game thread -> UI thread) ---
// Published by the game loop each frame; the Java overlay polls it to decide
// which on-screen controls to show. Values match touchhud::HudContext.
void SetHudContext(uint32_t context);
uint32_t LoadHudContext();

// --- Touch-as-mouse (input thread -> game thread) ---
// Feeds the GLES display's mouse stubs so the engine's own menu code
// (hover + left-click) handles touch directly, exactly like a PC mouse.
void SetTouchMouse(float x, float y, bool down);
// Returns true while a finger is down; always writes the latest position.
bool GetTouchMouse(float* x, float* y);

// --- Multi-touch for on-screen controls (input thread -> game thread) ---
static constexpr int32_t kMaxTouchPoints = 4;
struct TouchPoint {
    int32_t id = -1;   // Android pointer id, -1 = slot free
    float x = 0.0f;    // surface pixel coordinates
    float y = 0.0f;
    bool active = false;
};

void SetTouchPoint(int32_t slot, int32_t id, float x, float y, bool active);
void ClearTouchPoint(int32_t slot);
int32_t LoadTouchPoints(TouchPoint* out, int32_t max);

// One-shot tap notification consumed by the game loop (used to turn a tap
// into a Start press on the "press any button" title screen).
void QueueTouchTap();
bool ConsumeTouchTap();

// Touch tap position for direct menu hit-testing (input thread → game thread).
// The game thread processes these each frame in the menu's Invoke().
void QueueTouchTapPos(float x, float y);
bool ConsumeTouchTapPos(float* x, float* y);

} // namespace androidbridge
