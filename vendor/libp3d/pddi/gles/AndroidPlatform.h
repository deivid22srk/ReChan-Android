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

} // namespace androidbridge
