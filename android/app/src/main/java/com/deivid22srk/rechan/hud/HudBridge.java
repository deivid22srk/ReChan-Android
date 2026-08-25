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

    /** Current gameplay context (touchhud::HudContext). */
    public static native int nativePollContext();
}
