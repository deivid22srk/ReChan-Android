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
int32_t s_touchPointer = -1;

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

    // Touchscreen: emulate a mouse for the engine's own menu code (hover +
    // left-click) and queue taps for game-loop decisions (title screen).
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) != 0) {
        const int32_t action = AMotionEvent_getAction(event);
        const int32_t actionCode = action & AMOTION_EVENT_ACTION_MASK;
        const int32_t idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        switch (actionCode) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                if (s_touchPointer == -1) {
                    s_touchPointer = AMotionEvent_getPointerId(event, idx);
                    androidbridge::SetTouchMouse(
                        AMotionEvent_getX(event, idx), AMotionEvent_getY(event, idx), true);
                    androidbridge::QueueTouchTap();
                }
                return true;
            }
            case AMOTION_EVENT_ACTION_MOVE: {
                for (int32_t i = 0; i < AMotionEvent_getPointerCount(event); ++i) {
                    if (AMotionEvent_getPointerId(event, i) == s_touchPointer) {
                        androidbridge::SetTouchMouse(
                            AMotionEvent_getX(event, i), AMotionEvent_getY(event, i), true);
                        break;
                    }
                }
                return true;
            }
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
            case AMOTION_EVENT_ACTION_CANCEL: {
                const bool primaryLifted =
                    (actionCode == AMOTION_EVENT_ACTION_UP) ||
                    (actionCode == AMOTION_EVENT_ACTION_CANCEL) ||
                    (AMotionEvent_getPointerId(event, idx) == s_touchPointer);
                if (primaryLifted && s_touchPointer != -1) {
                    // A lift at the (possibly moved) position is a tap: publish
                    // it for direct menu hit-testing on the game thread.
                    if (actionCode != AMOTION_EVENT_ACTION_CANCEL) {
                        androidbridge::QueueTouchTapPos(
                            AMotionEvent_getX(event, idx), AMotionEvent_getY(event, idx));
                    }
                    s_touchPointer = -1;
                    androidbridge::SetTouchMouse(0.0f, 0.0f, false);
                }
                return true;
            }
            default:
                return true;
        }
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
