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
std::atomic<bool> g_padConnected{false};
std::atomic<uint32_t> g_hudContext{0};
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

static_assert(GamepadButton::COUNT <= 32, "buttons fit in u32 bitmask");
static_assert(GamepadAxis::COUNT == 6, "PadSnapshot axes size matches");

} // namespace androidbridge
