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

// --- Physical gamepad presence (Java InputDevice listener -> game thread) ---
// True while a physical (Bluetooth/USB) gamepad is connected. Completely
// independent of the virtual touch pad above: PostGamepadConnected drives the
// ENGINE's "is a pad plugged in" mode (which stays true on this touch-first
// build), while this flag only decides whether the on-screen touch HUD
// should be visible. Producers: the Java InputDevice.InputDeviceListener
// (GameActivity) for connect AND disconnect, plus a native self-confirm when
// gamepad-sourced events arrive (AndroidInput.cpp). Consumer: the game thread
// (touchhud/touchcontrols) once per frame.
void SetPhysicalPadConnected(bool connected);
bool IsPhysicalPadConnected();

// --- Ghost pad devices (Java InputDevice scan -> input thread) ---
// Xiaomi/MIUI exposes fingerprint readers and other sensor hubs through
// uinput (uinput-fpc, uinput-goodix, uinput-synaptics, uinput-elan,
// uinput-vfs, uinput-atrus) as input devices whose sources mask LIES:
// SOURCE_GAMEPAD|SOURCE_JOYSTICK are set while isVirtual() reports false.
// Without filtering, every Xiaomi/Redmi/POCO phone sees a "physical
// gamepad" at boot and the touch HUD never appears (documented across
// engines: Godot #47656, libgdx #5596, Unity forums, GameMaker on Redmi).
// GameActivity pushes the ghost device ids it finds; AndroidInput.cpp then
// swallows their events entirely - no phantom pad-presence confirm, no
// phantom buttons/axes (fingerprint swipe gestures would otherwise drive
// the virtual stick). Single writer (Java thread): ids are stored first
// and the count last (release), so readers always observe a consistent
// prefix.
void SetGhostPadDeviceIds(const int32_t* ids, int32_t count);
bool IsGhostPadDeviceId(int32_t deviceId);

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
    // Bumped every time a slot is (re)acquired by a DOWN. Android recycles
    // pointer ids across gestures, so id alone can't tell "same finger that
    // grabbed the control" from "new finger that recycled the id within one
    // frame" — the seq can.
    uint32_t seq = 0;
};

void SetTouchPoint(int32_t slot, int32_t id, float x, float y, bool active);
void ClearTouchPoint(int32_t slot);
int32_t LoadTouchPoints(TouchPoint* out, int32_t max);

// Real-slot helpers. CRITICAL: LoadTouchPoints() returns the active points
// COMPACTED (packed at the front), so its indices are NOT slot indices.
// Never use compacted indices with Set/ClearTouchPoint — that wrote a moving
// finger into the wrong slot, duplicated its pointer id, and stranded
// "zombie" slots that stayed active forever (stuck joystick + dead buttons).
// These helpers index g_touchSlots directly:
//   FindTouchSlotById  -> slot holding pointerId, or -1.
//   AcquireTouchSlot   -> reuses the slot already holding pointerId (self-
//                         heals a stale entry), else claims the first free
//                         slot; writes id/x/y and marks it active. -1 if full.
//   ClearAllTouchPoints-> releases every slot (ACTION_CANCEL aborts the WHOLE
//                         gesture; lifecycle pause/resume must also drop all
//                         fingers, since Android will never send their UP).
int32_t FindTouchSlotById(int32_t pointerId);
int32_t AcquireTouchSlot(int32_t pointerId, float x, float y);
void ClearAllTouchPoints();

// One-shot tap notification consumed by the game loop (used to turn a tap
// into a Start press on the "press any button" title screen).
void QueueTouchTap();
bool ConsumeTouchTap();

// Touch tap position for direct menu hit-testing (input thread → game thread).
// The game thread processes these each frame in the menu's Invoke().
void QueueTouchTapPos(float x, float y);
bool ConsumeTouchTapPos(float* x, float* y);

} // namespace androidbridge
