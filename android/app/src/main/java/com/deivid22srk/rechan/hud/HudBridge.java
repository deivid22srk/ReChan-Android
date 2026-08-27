package com.deivid22srk.rechan.hud;

/**
 * JNI bridge between the touch overlay (Java) and the engine (librechan.so).
 *
 * Touch input feeds the exact same androidbridge gamepad state a physical
 * gamepad uses, so the engine sees one merged virtual pad and there is no
 * duplicate input path. Context values must match touchhud::HudContext
 * (src/extra/touchhud.h).
 */
public final class HudBridge {
    // touchhud::HudContext
    public static final int CONTEXT_HIDDEN = 0;
    public static final int CONTEXT_ON_FOOT = 1;
    public static final int CONTEXT_CLIMBING = 2;
    public static final int CONTEXT_MENU = 3;

    // pddi GamepadButton (GLFW-style layout)
    public static final int BTN_A = 0;
    public static final int BTN_B = 1;
    public static final int BTN_X = 2;
    public static final int BTN_Y = 3;
    public static final int BTN_LB = 4;
    public static final int BTN_RB = 5;
    public static final int BTN_START = 7;

    // pddi GamepadAxis
    public static final int AXIS_LEFT_X = 0;
    public static final int AXIS_LEFT_Y = 1;

    static {
        // Already loaded by NativeActivity; loadLibrary is idempotent.
        System.loadLibrary("rechan");
    }

    private HudBridge() {}

    public static native void nativePostButton(int button, boolean down);

    public static native void nativePostAxis(int axis, float value);

    public static native void nativePostConnected(boolean connected);

    /**
     * Physical (Bluetooth/USB) gamepad presence, pushed by GameActivity's
     * InputDevice listener. While true the engine hides the on-screen touch
     * HUD (the real pad is the input); when the last pad disconnects the HUD
     * comes back.
     */
    public static native void nativeSetPhysicalGamepad(boolean connected);

    /**
     * Device ids of "ghost" input devices that must never be treated as
     * gamepads: Xiaomi/MIUI exposes its fingerprint reader (uinput-fpc,
     * uinput-goodix, uinput-synaptics, ...) with SOURCE_GAMEPAD|
     * SOURCE_JOYSTICK sources while isVirtual() reports false. The engine
     * swallows events from these ids so they can neither confirm
     * physical-pad presence nor inject phantom buttons/axes (fingerprint
     * swipe gestures would otherwise drive the virtual stick).
     */
    public static native void nativeSetGhostPadDeviceIds(int[] deviceIds);

    /** Current gameplay context (touchhud::HudContext). */
    public static native int nativePollContext();
}
