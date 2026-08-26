// AndroidInput.cpp — AInputEvent translation into androidbridge gamepad state.
// Buttons arrive as KEY events (BUTTON_*, DPAD_*), sticks/triggers as MOTION
// events, D-pads sometimes as HAT axes on the motion source. Multiple pads are
// merged into one virtual pad (the menu is single-player only).
#include <android/input.h>

#include "pddi/gles/AndroidPlatform.h"
#include "pddi/pddidev.h"

namespace {

constexpr int32_t kSourceGamepadMask =
    AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK | AINPUT_SOURCE_DPAD;

struct KeyButtonEntry {
    int32_t keyCode;
    int pddiButton;
};

constexpr KeyButtonEntry kKeyButtons[] = {
    {AKEYCODE_BUTTON_A, GamepadButton::A},
    {AKEYCODE_BUTTON_B, GamepadButton::B},
    {AKEYCODE_BUTTON_X, GamepadButton::X},
    {AKEYCODE_BUTTON_Y, GamepadButton::Y},
    {AKEYCODE_BUTTON_L1, GamepadButton::LeftBumper},
    {AKEYCODE_BUTTON_R1, GamepadButton::RightBumper},
    {AKEYCODE_BUTTON_SELECT, GamepadButton::Back},
    {AKEYCODE_BUTTON_START, GamepadButton::Start},
    {AKEYCODE_BUTTON_MODE, GamepadButton::Guide},
    {AKEYCODE_BUTTON_THUMBL, GamepadButton::LeftThumb},
    {AKEYCODE_BUTTON_THUMBR, GamepadButton::RightThumb},
    {AKEYCODE_DPAD_UP, GamepadButton::DpadUp},
    {AKEYCODE_DPAD_RIGHT, GamepadButton::DpadRight},
    {AKEYCODE_DPAD_DOWN, GamepadButton::DpadDown},
    {AKEYCODE_DPAD_LEFT, GamepadButton::DpadLeft},
    // Digital L2/R2 drive the trigger axes (GLFW convention: -1 released).
    {AKEYCODE_BUTTON_L2, -100}, // special: LeftTrigger axis
    {AKEYCODE_BUTTON_R2, -101}, // special: RightTrigger axis
};

// BACK and MENU open the in-game menu (never exit the app; quitting is done
// through the game's own Quit screen).
bool IsMenuKey(int32_t keyCode) {
    return keyCode == AKEYCODE_BACK || keyCode == AKEYCODE_MENU;
}

const KeyButtonEntry* FindKeyEntry(int32_t keyCode) {
    for (const auto& entry : kKeyButtons) {
        if (entry.keyCode == keyCode) return &entry;
    }
    return nullptr;
}

float ClampAxis(float v) {
    if (v > 1.0f) return 1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

int HatToButtons(float hatX, float hatY) {
    int buttons = 0;
    const int kUp = 1 << GamepadButton::DpadUp;
    const int kDown = 1 << GamepadButton::DpadDown;
    const int kLeft = 1 << GamepadButton::DpadLeft;
    const int kRight = 1 << GamepadButton::DpadRight;
    if (hatX < -0.5f) buttons |= kLeft;
    if (hatX > 0.5f) buttons |= kRight;
    if (hatY < -0.5f) buttons |= kUp;   // HAT_Y: -1 = up
    if (hatY > 0.5f) buttons |= kDown;  // +1 = down
    return buttons;
}

// Pointer id of the first finger driving the emulated mouse (-1 = none).
// The multi-touch slots (androidbridge) track all fingers; the primary
// index is the first slot that was assigned (used for menu hover).
int32_t s_touchPointer = -1;

static void AdoptPrimaryFromSlots() {
    // Primary finger went away — either adopt another live finger or reset
    // the emulated mouse. Runs on the input thread only.
    androidbridge::TouchPoint pts[androidbridge::kMaxTouchPoints];
    const int32_t cnt = androidbridge::LoadTouchPoints(pts, androidbridge::kMaxTouchPoints);
    if (cnt > 0) {
        s_touchPointer = pts[0].id;
        androidbridge::SetTouchMouse(pts[0].x, pts[0].y, false);
    }
    else {
        s_touchPointer = -1;
        androidbridge::SetTouchMouse(0.0f, 0.0f, false);
    }
}

// Drops every finger at once. ACTION_CANCEL aborts the WHOLE gesture (not
// just one pointer), and lifecycle events (pause, lost focus, surface going
// away) mean Android will never deliver the missing UPs — leaving the slots
// set would freeze the joystick at its last deflection and keep "ghost"
// fingers hogging every slot, which is exactly how the HUD died before.
void RechanAndroidResetTouch() {
    s_touchPointer = -1;
    androidbridge::ClearAllTouchPoints();
    androidbridge::SetTouchMouse(0.0f, 0.0f, false);
}

static void HandleTouchEvent(AInputEvent* event) {
    const int32_t action = AMotionEvent_getAction(event);
    const int32_t actionCode = action & AMOTION_EVENT_ACTION_MASK;
    const int32_t idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                            >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    const int32_t pointerId = AMotionEvent_getPointerId(event, idx);
    const float x = AMotionEvent_getX(event, idx);
    const float y = AMotionEvent_getY(event, idx);

    switch (actionCode) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            // AcquireTouchSlot indexes the real bridge slots (reusing any
            // stale same-id entry) — never the compacted LoadTouchPoints()
            // view, which used to send a finger's data into the WRONG slot.
            if (androidbridge::AcquireTouchSlot(pointerId, x, y) >= 0) {
                if (s_touchPointer == -1) {
                    s_touchPointer = pointerId;
                    androidbridge::SetTouchMouse(x, y, false);
                    androidbridge::QueueTouchTap();
                }
            }
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE: {
            const int32_t pointerCount = AMotionEvent_getPointerCount(event);
            for (int32_t i = 0; i < pointerCount; ++i) {
                const int32_t pid = AMotionEvent_getPointerId(event, i);
                const float px = AMotionEvent_getX(event, i);
                const float py = AMotionEvent_getY(event, i);
                int32_t slot = androidbridge::FindTouchSlotById(pid);
                if (slot < 0) {
                    // Missed the DOWN (slot pressure at the time): self-heal
                    // by claiming a slot now instead of dropping the finger.
                    slot = androidbridge::AcquireTouchSlot(pid, px, py);
                    if (slot < 0) continue;
                }
                androidbridge::SetTouchPoint(slot, pid, px, py, true);
            }
            // Keep the emulated mouse on the primary finger.
            if (s_touchPointer >= 0) {
                androidbridge::TouchPoint pts[androidbridge::kMaxTouchPoints];
                const int32_t cnt = androidbridge::LoadTouchPoints(pts, androidbridge::kMaxTouchPoints);
                bool found = false;
                for (int32_t i = 0; i < cnt; ++i) {
                    if (pts[i].id == s_touchPointer) {
                        androidbridge::SetTouchMouse(pts[i].x, pts[i].y, false);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    AdoptPrimaryFromSlots();
                }
            }
            break;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            const int32_t slot = androidbridge::FindTouchSlotById(pointerId);
            if (slot >= 0) {
                androidbridge::QueueTouchTapPos(x, y);
                androidbridge::ClearTouchPoint(slot);
                if (pointerId == s_touchPointer) {
                    AdoptPrimaryFromSlots();
                }
            }
            break;
        }
        case AMOTION_EVENT_ACTION_CANCEL: {
            // CANCEL aborts the entire gesture — every finger is up as far as
            // the app is concerned. Clearing only the indexed pointer used to
            // strand the others as "zombie" slots (joystick stuck dragging,
            // buttons dead once the zombies filled all 4 slots).
            RechanAndroidResetTouch();
            break;
        }
        default:
            break;
    }
}

void ApplyHatButtons(int32_t deviceId, int wanted) {
    // Per-device diff state: a pad without HAT axes keeps emitting 0 and must
    // not clear the D-pad held on a different controller.
    struct HatState {
        int32_t deviceId;
        int applied;
    };
    static HatState states[8] = {};
    static size_t count = 0;
    HatState* state = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (states[i].deviceId == deviceId) {
            state = &states[i];
            break;
        }
    }
    if (!state && count < 8) {
        state = &states[count++];
        state->deviceId = deviceId;
        state->applied = 0;
    }
    if (!state) return;

    struct Diff {
        int bit;
        int button;
    };
    constexpr Diff diffs[] = {
        {1 << GamepadButton::DpadUp, GamepadButton::DpadUp},
        {1 << GamepadButton::DpadDown, GamepadButton::DpadDown},
        {1 << GamepadButton::DpadLeft, GamepadButton::DpadLeft},
        {1 << GamepadButton::DpadRight, GamepadButton::DpadRight},
    };
    for (const auto& d : diffs) {
        const bool wasDown = (state->applied & d.bit) != 0;
        const bool isDown = (wanted & d.bit) != 0;
        if (wasDown != isDown) {
            androidbridge::PostGamepadButton(d.button, isDown);
        }
    }
    state->applied = wanted;
}

bool HandleKeyEvent(AInputEvent* event) {
    const int32_t keyCode = AKeyEvent_getKeyCode(event);
    const bool down = AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN;

    if (IsMenuKey(keyCode)) {
        androidbridge::PostGamepadConnected(true);
        androidbridge::PostGamepadButton(GamepadButton::Start, down);
        return true;
    }

    if (const KeyButtonEntry* entry = FindKeyEntry(keyCode)) {
        androidbridge::PostGamepadConnected(true);
        if (entry->pddiButton >= 0) {
            androidbridge::PostGamepadButton(entry->pddiButton, down);
        }
        else if (entry->pddiButton == -100) {
            androidbridge::PostGamepadAxis(GamepadAxis::LeftTrigger, down ? 1.0f : -1.0f);
        }
        else if (entry->pddiButton == -101) {
            androidbridge::PostGamepadAxis(GamepadAxis::RightTrigger, down ? 1.0f : -1.0f);
        }
        return true;
    }

    // Consume gamepad-sourced keys we don't map so they don't leak elsewhere.
    return false;
}

bool HandleMotionEvent(AInputEvent* event) {
    const int32_t source = AInputEvent_getSource(event);

    // Touchscreen: feed the mouse POSITION for hover only (button state is
    // intentionally NOT emulated — direct taps handled by the menu's
    // ProcessTouchTaps would otherwise double-activate alongside a mouse
    // left-click Confirm). Multi-touch slots drive the on-screen controls.
    // Require BOTH source bits (class-pointer + touchscreen): mice, styluses
    // and touchpads share the pointer class bit, and treating their events
    // as finger input made the engine "recognize a mouse" that doesn't
    // exist while the virtual gamepad was also active.
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) {
        HandleTouchEvent(event);
        return true;
    }

    if ((source & kSourceGamepadMask) == 0) {
        return false;
    }

    androidbridge::PostGamepadConnected(true);

    const float lx = ClampAxis(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0));
    const float ly = ClampAxis(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0));
    const float rx = ClampAxis(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0));
    const float ry = ClampAxis(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0));

    androidbridge::PostGamepadAxis(GamepadAxis::LeftX, lx);
    // Android AXIS_Y already matches the GLFW gamepad convention
    // (-1 = up, +1 = down), so no sign flip here (unlike libnx, which is
    // up-positive and needs negating on the Switch branch).
    androidbridge::PostGamepadAxis(GamepadAxis::LeftY, ly);
    androidbridge::PostGamepadAxis(GamepadAxis::RightX, rx);
    androidbridge::PostGamepadAxis(GamepadAxis::RightY, ry);

    float ltrig = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float rtrig = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    if (ltrig <= 0.0f) ltrig = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0);
    if (rtrig <= 0.0f) rtrig = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0);
    androidbridge::PostGamepadAxis(GamepadAxis::LeftTrigger, ClampAxis(ltrig * 2.0f - 1.0f));
    androidbridge::PostGamepadAxis(GamepadAxis::RightTrigger, ClampAxis(rtrig * 2.0f - 1.0f));

    ApplyHatButtons(
        AInputEvent_getDeviceId(event),
        HatToButtons(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0),
                     AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0)));

    return true;
}

} // namespace

bool RechanAndroidHandleInputEvent(AInputEvent* event) {
    switch (AInputEvent_getType(event)) {
        case AINPUT_EVENT_TYPE_KEY:
            return HandleKeyEvent(event);
        case AINPUT_EVENT_TYPE_MOTION:
            return HandleMotionEvent(event);
        default:
            return false;
    }
}
