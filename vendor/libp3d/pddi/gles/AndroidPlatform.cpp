// AndroidPlatform.cpp — androidbridge implementation shared between the
// NativeActivity shell and the GLES pddi backend.
#include "pddi/gles/AndroidPlatform.h"

#include "pddi/pddidev.h"

namespace androidbridge {

namespace {
std::atomic<NativeWindowHandle> g_currentWindow{nullptr};
std::atomic<NativeWindowHandle> g_pendingWindow{nullptr};
std::atomic<uint64_t> g_windowGeneration{0};
std::atomic<bool> g_exitRequested{false};
std::atomic<uint32_t> g_heldButtons{0};
std::atomic<float> g_axes[GamepadAxis::COUNT] = {};
// Touch-first device: the on-screen HUD feeds a virtual gamepad that is
// always present, so the engine starts (and stays) in gamepad mode —
// analog pad type, gamepad prompt glyphs and pad menu navigation work
// from the first frame, before the first touch. Physical pad / on-screen
// presses merely keep the flag set.
std::atomic<bool> g_padConnected{true};
std::atomic<uint32_t> g_hudContext{0};
std::atomic<float> g_touchX{0.0f};
std::atomic<float> g_touchY{0.0f};
std::atomic<bool> g_touchDown{false};
std::atomic<bool> g_tapQueued{false};
std::atomic<bool> g_tapPosQueued{false};
std::atomic<float> g_tapPosX{0.0f};
std::atomic<float> g_tapPosY{0.0f};

// Multi-touch slots for the on-screen controls.
struct TouchSlot {
    std::atomic<int32_t> id{-1};
    std::atomic<float> x{0.0f};
    std::atomic<float> y{0.0f};
    std::atomic<bool> active{false};
    std::atomic<uint32_t> seq{0}; // bumped on every (re)acquire (DOWN)
};
TouchSlot g_touchSlots[kMaxTouchPoints];
} // namespace

void SetNativeWindow(NativeWindowHandle window) {
    if (window) {
        g_pendingWindow.store(window, std::memory_order_release);
        g_currentWindow.store(window, std::memory_order_release);
    }
    else {
        // Clear BOTH: a stale pending handle would otherwise hand a dead
        // window back to the display's resume path.
        g_currentWindow.store(nullptr, std::memory_order_release);
        g_pendingWindow.store(nullptr, std::memory_order_release);
    }
    g_windowGeneration.fetch_add(1, std::memory_order_release);
}

void RequestExit() { g_exitRequested.store(true, std::memory_order_release); }

NativeWindowHandle TakePendingWindow() {
    return g_pendingWindow.exchange(nullptr, std::memory_order_acq_rel);
}

NativeWindowHandle PeekCurrentWindow() {
    return g_currentWindow.load(std::memory_order_acquire);
}

uint64_t WindowGeneration() {
    return g_windowGeneration.load(std::memory_order_acquire);
}

bool ExitRequested() { return g_exitRequested.load(std::memory_order_acquire); }

void PostGamepadButton(int pddiButton, bool down) {
    if (pddiButton < 0 || pddiButton >= GamepadButton::COUNT) return;
    const uint32_t bit = 1u << pddiButton;
    if (down) {
        g_heldButtons.fetch_or(bit, std::memory_order_relaxed);
    }
    else {
        g_heldButtons.fetch_and(~bit, std::memory_order_relaxed);
    }
}

void PostGamepadAxis(int pddiAxis, float value) {
    if (pddiAxis < 0 || pddiAxis >= GamepadAxis::COUNT) return;
    g_axes[pddiAxis].store(value, std::memory_order_relaxed);
}

void PostGamepadConnected(bool connected) {
    g_padConnected.store(connected, std::memory_order_relaxed);
}

PadSnapshot LoadPadSnapshot() {
    PadSnapshot snap;
    snap.heldButtons = g_heldButtons.load(std::memory_order_relaxed);
    for (int i = 0; i < GamepadAxis::COUNT; ++i) {
        snap.axes[i] = g_axes[i].load(std::memory_order_relaxed);
    }
    snap.connected = g_padConnected.load(std::memory_order_relaxed);
    return snap;
}

void SetHudContext(uint32_t context) {
    g_hudContext.store(context, std::memory_order_relaxed);
}

uint32_t LoadHudContext() {
    return g_hudContext.load(std::memory_order_relaxed);
}

void SetTouchMouse(float x, float y, bool down) {
    g_touchX.store(x, std::memory_order_relaxed);
    g_touchY.store(y, std::memory_order_relaxed);
    g_touchDown.store(down, std::memory_order_relaxed);
}

bool GetTouchMouse(float* x, float* y) {
    if (x) x[0] = g_touchX.load(std::memory_order_relaxed);
    if (y) y[0] = g_touchY.load(std::memory_order_relaxed);
    return g_touchDown.load(std::memory_order_relaxed);
}

void QueueTouchTap() {
    g_tapQueued.store(true, std::memory_order_release);
}

bool ConsumeTouchTap() {
    return g_tapQueued.exchange(false, std::memory_order_acq_rel);
}

void QueueTouchTapPos(float x, float y) {
    g_tapPosX.store(x, std::memory_order_relaxed);
    g_tapPosY.store(y, std::memory_order_relaxed);
    g_tapPosQueued.store(true, std::memory_order_release);
}

bool ConsumeTouchTapPos(float* x, float* y) {
    if (!g_tapPosQueued.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    if (x) x[0] = g_tapPosX.load(std::memory_order_relaxed);
    if (y) y[0] = g_tapPosY.load(std::memory_order_relaxed);
    return true;
}

void SetTouchPoint(int32_t slot, int32_t id, float x, float y, bool active) {
    if (slot < 0 || slot >= kMaxTouchPoints) return;
    g_touchSlots[slot].id.store(id, std::memory_order_relaxed);
    g_touchSlots[slot].x.store(x, std::memory_order_relaxed);
    g_touchSlots[slot].y.store(y, std::memory_order_relaxed);
    g_touchSlots[slot].active.store(active, std::memory_order_relaxed);
}

void ClearTouchPoint(int32_t slot) {
    if (slot < 0 || slot >= kMaxTouchPoints) return;
    g_touchSlots[slot].id.store(-1, std::memory_order_relaxed);
    g_touchSlots[slot].active.store(false, std::memory_order_relaxed);
}

int32_t LoadTouchPoints(TouchPoint* out, int32_t max) {
    int32_t count = 0;
    for (int32_t s = 0; s < kMaxTouchPoints && count < max; ++s) {
        // acquire pairs with AcquireTouchSlot's release store of active,
        // so the id/x/y/seq written before it are visible once active.
        if (!g_touchSlots[s].active.load(std::memory_order_acquire)) continue;
        TouchPoint& p = out[count++];
        p.id = g_touchSlots[s].id.load(std::memory_order_relaxed);
        p.x = g_touchSlots[s].x.load(std::memory_order_relaxed);
        p.y = g_touchSlots[s].y.load(std::memory_order_relaxed);
        p.active = true;
        p.seq = g_touchSlots[s].seq.load(std::memory_order_relaxed);
    }
    return count;
}

int32_t FindTouchSlotById(int32_t pointerId) {
    if (pointerId < 0) return -1;
    for (int32_t s = 0; s < kMaxTouchPoints; ++s) {
        if (!g_touchSlots[s].active.load(std::memory_order_acquire)) continue;
        if (g_touchSlots[s].id.load(std::memory_order_relaxed) == pointerId) return s;
    }
    return -1;
}

int32_t AcquireTouchSlot(int32_t pointerId, float x, float y) {
    if (pointerId < 0) return -1;
    // Reuse the entry already holding this id if any (id collision after a
    // missed UP self-heals here instead of leaking a duplicate).
    int32_t slot = FindTouchSlotById(pointerId);
    if (slot < 0) {
        for (int32_t s = 0; s < kMaxTouchPoints; ++s) {
            if (!g_touchSlots[s].active.load(std::memory_order_relaxed)) {
                slot = s;
                break;
            }
        }
    }
    if (slot < 0) return -1; // all slots busy
    // Write id/x/y + a FRESH seq, active last so the game thread never sees
    // a new activation paired with the previous touch's seq.
    g_touchSlots[slot].id.store(pointerId, std::memory_order_relaxed);
    g_touchSlots[slot].x.store(x, std::memory_order_relaxed);
    g_touchSlots[slot].y.store(y, std::memory_order_relaxed);
    g_touchSlots[slot].seq.fetch_add(1, std::memory_order_relaxed);
    g_touchSlots[slot].active.store(true, std::memory_order_release);
    return slot;
}

void ClearAllTouchPoints() {
    for (int32_t s = 0; s < kMaxTouchPoints; ++s) {
        ClearTouchPoint(s);
    }
}

static_assert(GamepadButton::COUNT <= 32, "buttons fit in u32 bitmask");
static_assert(GamepadAxis::COUNT == 6, "PadSnapshot axes size matches");

} // namespace androidbridge
