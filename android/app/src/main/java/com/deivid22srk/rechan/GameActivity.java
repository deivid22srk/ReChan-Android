package com.deivid22srk.rechan;

import android.app.NativeActivity;
import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import java.util.ArrayList;
import java.util.List;

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
        implements InputManager.InputDeviceListener {

    private static final String TAG = "ReChan";

    private InputManager inputManager;
    private Boolean lastPushedPhysicalPad = null;

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
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) {
            inputManager.registerInputDeviceListener(this, null);
        }
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
        if (inputManager != null) {
            inputManager.unregisterInputDeviceListener(this);
        }
        super.onDestroy();
    }

    // --- InputManager.InputDeviceListener ---

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
        boolean physicalPad = false;
        final List<Integer> ghostIds = new ArrayList<>();
        // No early return: even after a real pad is found the scan keeps
        // going so EVERY ghost device id reaches the engine's blacklist.
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null) continue;
            if (isGhostInputDevice(device)) {
                ghostIds.add(id);
                continue;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && device.isVirtual()) {
                continue;
            }
            if (isPhysicalGamepadDevice(device)) {
                physicalPad = true;
            }
        }
        HudBridge.nativeSetPhysicalGamepad(physicalPad);
        final int[] ghosts = new int[ghostIds.size()];
        for (int i = 0; i < ghosts.length; i++) ghosts[i] = ghostIds.get(i);
        HudBridge.nativeSetGhostPadDeviceIds(ghosts);
        if (lastPushedPhysicalPad == null || lastPushedPhysicalPad != physicalPad) {
            Log.i(TAG, "physical gamepad "
                    + (physicalPad ? "connected" : "disconnected")
                    + " (ghost devices filtered: " + ghosts.length + ")");
            lastPushedPhysicalPad = physicalPad;
        }
    }

    /**
     * Xiaomi/MIUI exposes fingerprint readers and other sensor hubs through
     * uinput as input devices that LIE in their sources mask: SOURCE_GAMEPAD
     * and SOURCE_JOYSTICK are set while isVirtual() reports false. Without
     * filtering, every Xiaomi/Redmi/POCO phone sees a "physical gamepad" at
     * boot and the touch HUD never appears. Known ghost names: uinput-fpc,
     * uinput-goodix, uinput-synaptics, uinput-elan, uinput-vfs,
     * uinput-atrus (documented across engines: Godot #47656, libgdx #5596,
     * Unity forums, GameMaker on Redmi). None of them can play the game.
     */
    private static boolean isGhostInputDevice(InputDevice device) {
        final String name = device.getName();
        if (name == null) return false;
        final String n = name.trim().toLowerCase();
        return n.startsWith("uinput-") || n.equals("uinput");
    }

    /**
     * True for real, playable pads (Bluetooth/USB). Ghost devices (uinput-*)
     * and virtual devices must be filtered BEFORE calling this. Also
     * deliberately excludes:
     *   - keyboards (SOURCE_KEYBOARD) — they can't play;
     *   - DPAD-only TV remotes (SOURCE_DPAD) — no analog stick, no face
     *     buttons; the touch HUD stays useful with them.
     * Same criteria as the (disabled) HudController.isRealGamepad.
     */
    private static boolean isPhysicalGamepadDevice(InputDevice device) {
        final int sources = device.getSources();
        final boolean gamepadSrc =
                (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
        final boolean joystickSrc =
                (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        if (!gamepadSrc && !joystickSrc) return false;
        if (joystickSrc && !gamepadSrc) {
            // Joystick source without stick axes: some odd devices report
            // the source bit but expose no ranges — not a playable pad.
            for (InputDevice.MotionRange range : device.getMotionRanges()) {
                if ((range.getSource() & InputDevice.SOURCE_JOYSTICK)
                        == InputDevice.SOURCE_JOYSTICK) {
                    return true;
                }
            }
            return false;
        }
        return true;
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
