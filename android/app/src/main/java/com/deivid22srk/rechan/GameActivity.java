package com.deivid22srk.rechan;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import com.deivid22srk.rechan.hud.HudBridge;

/**
 * NativeActivity hosting the ReChan GLES3 engine. Immersive fullscreen with
 * cutout support; all input flows through the native input queue.
 *
 * The engine handles touch natively (on-screen contextual HUD in
 * touchcontrols.cpp; menu taps in fecustommenumgr.cpp).
 *
 * Physical gamepad hot-plug: an InputDevice listener watches for pads being
 * added/removed and pushes the presence flag into the engine, so the
 * on-screen touch HUD hides while a real (Bluetooth/USB) pad is connected and
 * comes back when the last pad disconnects.
 */
public class GameActivity extends NativeActivity
        implements InputDevice.InputDeviceListener {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // Watch for physical gamepads connecting/disconnecting while the
        // activity lives. Null handler -> callbacks on the main thread
        // (this thread, which has a Looper).
        InputDevice.registerInputDeviceListener(this, null);
        updatePhysicalGamepadState();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Pads that connected while the activity was down (or that the listener
        // missed) are caught by the rescan.
        updatePhysicalGamepadState();
    }

    @Override
    protected void onDestroy() {
        InputDevice.unregisterInputDeviceListener(this);
        super.onDestroy();
    }

    // --- InputDevice.InputDeviceListener ---

    @Override
    public void onInputDeviceAdded(int deviceId) {
        updatePhysicalGamepadState();
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        updatePhysicalGamepadState();
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        updatePhysicalGamepadState();
    }

    private void updatePhysicalGamepadState() {
        HudBridge.nativeSetPhysicalGamepad(hasPhysicalGamepad());
    }

    /**
     * True while at least one non-virtual input device reports gamepad or
     * joystick sources (joystick-source devices must really expose stick
     * axes). Deliberately excludes:
     *   - keyboards (SOURCE_KEYBOARD) — they can't play;
     *   - DPAD-only TV remotes (SOURCE_DPAD) — no analog stick, no face
     *     buttons; the touch HUD stays useful with them.
     * Same criteria as the (disabled) HudController.isRealGamepad.
     */
    private static boolean hasPhysicalGamepad() {
        final int[] ids = InputDevice.getDeviceIds();
        for (int id : ids) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null) continue;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && device.isVirtual()) {
                continue;
            }
            final int sources = device.getSources();
            final boolean gamepadSrc =
                    (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
            final boolean joystickSrc =
                    (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
            if (!gamepadSrc && !joystickSrc) continue;
            if (joystickSrc && !gamepadSrc) {
                // Joystick source without stick axes: some odd devices report
                // the source bit but expose no ranges — not a playable pad.
                boolean hasStickAxes = false;
                for (InputDevice.MotionRange range : device.getMotionRanges()) {
                    if ((range.getSource() & InputDevice.SOURCE_JOYSTICK)
                            == InputDevice.SOURCE_JOYSTICK) {
                        hasStickAxes = true;
                        break;
                    }
                }
                if (!hasStickAxes) continue;
            }
            return true;
        }
        return false;
    }

    // --- Immersive fullscreen ---

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUi();
        }
    }

    private void hideSystemUi() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars()
                        | WindowInsets.Type.displayCutout());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
        else {
            //noinspection deprecation
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
