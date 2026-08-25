package com.deivid22srk.rechan.hud;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import org.json.JSONException;
import org.json.JSONObject;

/**
 * Owns the touch overlay lifecycle:
 *  - physical gamepad hot-plug detection (auto hide/show, no user action);
 *  - gameplay context polling (engine -> HUD) with smooth transitions;
 *  - persistence of the user layout (positions/scale/opacity/contrast).
 *
 * Decoupled from the engine: all communication goes through HudBridge
 * (JNI) — the view never touches engine state directly.
 */
public class HudController implements InputManager.InputDeviceListener {

    private static final String TAG = "rechan-hud";
    private static final boolean DEBUG = Log.isLoggable(TAG, Log.DEBUG);
    private static final String PREFS = "rechan_hud";
    private static final String KEY_LAYOUT = "layout_json";
    private static final long CONTEXT_POLL_MS = 100;

    private final Activity activity;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TouchHudView hudView;
    private InputManager inputManager;
    private boolean gamepadConnected;
    private boolean running;
    private int lastContext = -1;

    public HudController(Activity activity) {
        this.activity = activity;
    }

    /** Attach the overlay to the activity and start listening. */
    public void attach() {
        hudView = new TouchHudView(activity, this);
        hudView.setVisibility(View.GONE); // shown once state settles
        activity.addContentView(
                hudView,
                new android.view.ViewGroup.LayoutParams(
                        android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                        android.view.ViewGroup.LayoutParams.MATCH_PARENT));

        // Safe areas (cutout + system bars): recompute default layout bounds.
        // WindowInsets.Type / getInsets() require API 30; older devices run
        // without safe-area adjustment.
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            hudView.setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars()
                                | WindowInsets.Type.displayCutout());
                hudView.setSafeInsets(bars.left, bars.top, bars.right, bars.bottom);
                return insets;
            });
        }

        inputManager = (InputManager) activity.getSystemService(Context.INPUT_SERVICE);
        inputManager.registerInputDeviceListener(this, handler);
        gamepadConnected = anyRealGamepadConnected();
        applyGamepadVisibility(false);

        running = true;
        handler.postDelayed(contextPoller, CONTEXT_POLL_MS);
        log("attached; gamepadConnected=" + gamepadConnected);
    }

    public void detach() {
        running = false;
        handler.removeCallbacks(contextPoller);
        if (inputManager != null) {
            inputManager.unregisterInputDeviceListener(this);
        }
        if (hudView != null) {
            hudView.releaseAll();
        }
    }

    // --- Context polling (engine -> HUD) ------------------------------------

    private final Runnable contextPoller = new Runnable() {
        @Override
        public void run() {
            if (!running) return;
            int context = HudBridge.nativePollContext();
            if (context != lastContext) {
                log("context " + lastContext + " -> " + context);
                lastContext = context;
                applyContext(context);
            }
            handler.postDelayed(this, CONTEXT_POLL_MS);
        }
    };

    private void applyContext(int context) {
        if (gamepadConnected || context == HudBridge.CONTEXT_HIDDEN) {
            hudView.animateVisibility(false, context);
        }
        else {
            hudView.animateVisibility(true, context);
        }
    }

    // --- Physical gamepad hot-plug ------------------------------------------

    private void applyGamepadVisibility(boolean animate) {
        if (hudView == null) return;
        if (gamepadConnected) {
            hudView.releaseAll(); // no stuck virtual buttons while hidden
        }
        hudView.animateVisibility(!gamepadConnected, lastContext);
        if (animate) {
            log("gamepad " + (gamepadConnected ? "connected -> HUD hidden"
                                              : "disconnected -> HUD shown"));
        }
    }

    /** A "real" gamepad reports GAMEPAD or JOYSTICK sources with joystick
     *  axes; DPAD-only TV remotes and mice don't qualify. */
    private static boolean isRealGamepad(InputDevice device) {
        if (device == null || device.isVirtual()) return false;
        final int sources = device.getSources();
        final boolean gamepadSrc =
                (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
        final boolean joystickSrc =
                (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
        if (!gamepadSrc && !joystickSrc) return false;
        if (joystickSrc) {
            boolean hasStickAxes = false;
            for (InputDevice.MotionRange range : device.getMotionRanges()) {
                if ((range.getSource() & InputDevice.SOURCE_JOYSTICK)
                        == InputDevice.SOURCE_JOYSTICK) {
                    hasStickAxes = true;
                    break;
                }
            }
            if (!hasStickAxes) return false;
        }
        return true;
    }

    private boolean anyRealGamepadConnected() {
        for (int id : inputManager.getInputDeviceIds()) {
            if (isRealGamepad(inputManager.getInputDevice(id))) {
                return true;
            }
        }
        return false;
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        InputDevice device = inputManager.getInputDevice(deviceId);
        if (!isRealGamepad(device)) {
            log("ignored input device " + deviceId
                    + (device != null ? " sources=" + Integer.toHexString(device.getSources()) : ""));
            return;
        }
        gamepadConnected = true;
        applyGamepadVisibility(true);
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        // Re-scan: another pad may still be connected (multi-device case).
        boolean any = anyRealGamepadConnected();
        if (any != gamepadConnected) {
            gamepadConnected = any;
            applyGamepadVisibility(true);
        }
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        boolean any = anyRealGamepadConnected();
        if (any != gamepadConnected) {
            gamepadConnected = any;
            applyGamepadVisibility(true);
        }
    }

    // --- Layout persistence --------------------------------------------------

    void saveLayout(JSONObject layout) {
        activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit().putString(KEY_LAYOUT, layout.toString()).apply();
        log("layout saved");
    }

    JSONObject loadLayout() {
        try {
            String raw = activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                    .getString(KEY_LAYOUT, null);
            if (raw != null) {
                return new JSONObject(raw);
            }
        } catch (JSONException e) {
            log("layout parse failed: " + e.getMessage());
        }
        return null;
    }

    private static void log(String message) {
        if (DEBUG) {
            Log.d(TAG, message);
        }
    }
}
