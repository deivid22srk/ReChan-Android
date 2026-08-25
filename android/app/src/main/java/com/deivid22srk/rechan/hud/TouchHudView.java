package com.deivid22srk.rechan.hud;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Fullscreen transparent overlay that draws and handles the contextual touch
 * controls (virtual joystick + action buttons). Input is forwarded to the
 * engine through HudBridge (JNI) into the shared gamepad state — the engine
 * treats it exactly like a physical gamepad.
 *
 * Features: contextual control sets (per gameplay context), fade/scale
 * transitions, safe-area aware default layout, user layout editing
 * (long-press a control), per-control size, global opacity and low-contrast
 * mode, light haptic feedback, all persisted locally as JSON.
 */
public class TouchHudView extends View {

    private static final String TAG = "rechan-hud";
    private static final boolean DEBUG = Log.isLoggable(TAG, Log.DEBUG);
    private static final long EDIT_LONGPRESS_MS = 600;

    /** One on-screen control (button or the joystick). */
    private static class Control {
        final String id;
        final String label;
        final int buttonId; // -1 for the joystick
        float nx, ny;       // normalized center within the safe rect
        float nr;           // normalized radius (relative to min(safeW, safeH))
        boolean interactive = true;

        Control(String id, String label, int buttonId, float nx, float ny, float nr) {
            this.id = id;
            this.label = label;
            this.buttonId = buttonId;
            this.nx = nx;
            this.ny = ny;
            this.nr = nr;
        }
    }

    private final HudController controller;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private final List<Control> controls = new ArrayList<>();
    private Control joystick;

    private int insetL, insetT, insetR, insetB;
    private float opacity = 0.45f;
    private boolean lowContrast = false;
    private boolean editMode = false;
    private Control selectedControl;
    private boolean visible = false;
    private int context = HudBridge.CONTEXT_HIDDEN;

    // Pointer tracking: pointerId -> control id ("" for unassigned)
    private final android.util.SparseArray<String> pointerControls = new android.util.SparseArray<>();
    private final Set<Integer> pressedButtons = new HashSet<>();
    private int joyPointerId = -1;
    private float joyKnobDx, joyKnobDy;

    // Long-press -> edit mode
    private final Runnable longPressRunnable = this::enterEditMode;
    private float longPressStartX, longPressStartY;
    private int longPressPointer = -1;
    private boolean longPressPending;

    // Edit-mode chips
    private final List<RectF> chipBounds = new ArrayList<>();
    private static final String[] CHIPS = {"Menor", "Maior", "Opac-", "Opac+", "Contorno", "Concluir"};

    // Paints
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    public TouchHudView(Context context, HudController controller) {
        super(context);
        this.controller = controller;
        setFocusable(false);
        setClickable(true); // receive touch events
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);

        joystick = new Control("joy", "", -1, 0.12f, 0.70f, 0.105f);
        controls.add(joystick);
        controls.add(new Control("A", "A", HudBridge.BTN_A, 0.865f, 0.775f, 0.072f));
        controls.add(new Control("B", "B", HudBridge.BTN_B, 0.775f, 0.685f, 0.062f));
        controls.add(new Control("X", "X", HudBridge.BTN_X, 0.875f, 0.600f, 0.062f));
        controls.add(new Control("Y", "Y", HudBridge.BTN_Y, 0.965f, 0.690f, 0.062f));
        controls.add(new Control("RB", "R1", HudBridge.BTN_RB, 0.950f, 0.520f, 0.050f));
        controls.add(new Control("LB", "L1", HudBridge.BTN_LB, 0.045f, 0.280f, 0.050f));
        controls.add(new Control("ST", "≡", HudBridge.BTN_START, 0.500f, 0.940f, 0.042f));

        JSONObject saved = controller.loadLayout();
        if (saved != null) {
            applyLayout(saved);
        }

        fillPaint.setColor(Color.WHITE);
        strokePaint.setColor(Color.WHITE);
        strokePaint.setStyle(Paint.Style.STROKE);
        knobPaint.setColor(Color.WHITE);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setFakeBoldText(true);
    }

    // --- Public API (used by HudController) ----------------------------------

    void setSafeInsets(int l, int t, int r, int b) {
        insetL = l;
        insetT = t;
        insetR = r;
        insetB = b;
        invalidate();
    }

    /** Smooth show/hide with fade + slight scale; context picks active set. */
    void animateVisibility(boolean show, int hudContext) {
        this.context = hudContext;
        applyContextToControls();
        if (show == visible) {
            invalidate();
            return;
        }
        visible = show;
        if (show) {
            setVisibility(VISIBLE);
            setAlpha(0f);
            setScaleX(0.96f);
            setScaleY(0.96f);
            animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(180).start();
        }
        else {
            releaseAll();
            animate().alpha(0f).setDuration(180)
                    .withEndAction(() -> {
                        if (!visible) {
                            // INVISIBLE keeps the panel window sized; hidden
                            // views receive no touch dispatch anyway.
                            setVisibility(INVISIBLE);
                            exitEditMode();
                        }
                    }).start();
        }
        log("visibility -> " + show + " context=" + hudContext);
    }

    /** Release every virtual control (context change, gamepad connect, hide). */
    void releaseAll() {
        for (Control c : controls) {
            if (c.buttonId >= 0 && pressedButtons.contains(c.buttonId)) {
                HudBridge.nativePostButton(c.buttonId, false);
            }
        }
        pressedButtons.clear();
        if (joyPointerId != -1) {
            HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_X, 0f);
            HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_Y, 0f);
            joyPointerId = -1;
        }
        joyKnobDx = joyKnobDy = 0f;
        pointerControls.clear();
        handler.removeCallbacks(longPressRunnable);
        longPressPending = false;
        invalidate();
    }

    // --- Context handling -----------------------------------------------------

    private void applyContextToControls() {
        // Climbing: joystick + jump only. OnFoot: everything.
        boolean climbing = context == HudBridge.CONTEXT_CLIMBING;
        for (Control c : controls) {
            c.interactive = !climbing || c == joystick || "A".equals(c.id);
        }
    }

    // --- Layout persistence ----------------------------------------------------

    private JSONObject toJSON() {
        try {
            JSONObject root = new JSONObject();
            JSONObject ctrls = new JSONObject();
            for (Control c : controls) {
                ctrls.put(c.id, new JSONObject()
                        .put("x", c.nx).put("y", c.ny).put("r", c.nr));
            }
            root.put("controls", ctrls);
            root.put("opacity", opacity);
            root.put("lowContrast", lowContrast);
            return root;
        } catch (JSONException e) {
            return null;
        }
    }

    private void applyLayout(JSONObject root) {
        try {
            JSONObject ctrls = root.optJSONObject("controls");
            if (ctrls != null) {
                for (Control c : controls) {
                    JSONObject pos = ctrls.optJSONObject(c.id);
                    if (pos != null) {
                        c.nx = clamp01((float) pos.optDouble("x", c.nx));
                        c.ny = clamp01((float) pos.optDouble("y", c.ny));
                        c.nr = clamp01((float) pos.optDouble("r", c.nr));
                    }
                }
            }
            opacity = clamp01((float) root.optDouble("opacity", opacity));
            lowContrast = root.optBoolean("lowContrast", false);
        } catch (Exception e) {
            log("applyLayout failed: " + e.getMessage());
        }
    }

    private static float clamp01(float v) {
        return Math.max(0f, Math.min(1f, v));
    }

    // --- Geometry helpers -------------------------------------------------------

    private float safeW() {
        return Math.max(1, getWidth() - insetL - insetR);
    }

    private float safeH() {
        return Math.max(1, getHeight() - insetT - insetB);
    }

    private float cx(Control c) {
        return insetL + c.nx * safeW();
    }

    private float cy(Control c) {
        return insetT + c.ny * safeH();
    }

    private float radius(Control c) {
        return c.nr * Math.min(safeW(), safeH());
    }

    private Control controlAt(float x, float y) {
        Control hit = null;
        float bestDist = Float.MAX_VALUE;
        for (Control c : controls) {
            if (!c.interactive) continue;
            float dx = x - cx(c);
            float dy = y - cy(c);
            float dist = (float) Math.hypot(dx, dy);
            float reach = radius(c) * (c == joystick ? 1.6f : 1.35f);
            if (dist <= reach && dist < bestDist) {
                bestDist = dist;
                hit = c;
            }
        }
        return hit;
    }

    // --- Touch handling -----------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!visible || editMode) {
            return editMode ? handleEditTouch(event) : false;
        }

        final int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = event.getActionIndex();
                int pid = event.getPointerId(idx);
                float x = event.getX(idx);
                float y = event.getY(idx);
                Control c = controlAt(x, y);
                pointerControls.put(pid, c != null ? c.id : "");
                if (c != null) {
                    if (c == joystick) {
                        joyPointerId = pid;
                        updateJoystick(x, y);
                    }
                    else {
                        pressButton(c, true);
                    }
                    startLongPressDetection(pid, x, y);
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int pid = event.getPointerId(i);
                    String assigned = pointerControls.get(pid, "");
                    if (assigned.isEmpty()) continue;
                    Control c = findControl(assigned);
                    if (c == null) continue;
                    float x = event.getX(i);
                    float y = event.getY(i);
                    if (c == joystick && pid == joyPointerId) {
                        updateJoystick(x, y);
                    }
                    if (longPressPending && pid == longPressPointer) {
                        if (Math.hypot(x - longPressStartX, y - longPressStartY) > touchSlopPx()) {
                            cancelLongPressDetection();
                        }
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = event.getActionIndex();
                int pid = event.getPointerId(idx);
                releasePointer(pid);
                if (pid == joyPointerId) {
                    joyPointerId = -1;
                    joyKnobDx = joyKnobDy = 0f;
                    HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_X, 0f);
                    HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_Y, 0f);
                    invalidate();
                }
                if (pointerControls.size() == 0) {
                    cancelLongPressDetection();
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                releaseAll();
                return true;
            }
            default:
                return true;
        }
    }

    private void startLongPressDetection(int pid, float x, float y) {
        longPressStartX = x;
        longPressStartY = y;
        longPressPointer = pid;
        longPressPending = true;
        handler.removeCallbacks(longPressRunnable);
        handler.postDelayed(longPressRunnable, EDIT_LONGPRESS_MS);
    }

    private void cancelLongPressDetection() {
        handler.removeCallbacks(longPressRunnable);
        longPressPending = false;
        longPressPointer = -1;
    }

    private float touchSlopPx() {
        return android.view.ViewConfiguration.get(getContext()).getScaledTouchSlop() * 2f;
    }

    private void updateJoystick(float x, float y) {
        float dx = x - cx(joystick);
        float dy = y - cy(joystick);
        float r = radius(joystick);
        float mag = (float) Math.hypot(dx, dy);
        if (mag > r) {
            dx = dx / mag * r;
            dy = dy / mag * r;
        }
        joyKnobDx = dx;
        joyKnobDy = dy;
        // Screen Y is down-positive, matching the GLFW/gamepad axis convention.
        HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_X, clampAxis(dx / r));
        HudBridge.nativePostAxis(HudBridge.AXIS_LEFT_Y, clampAxis(dy / r));
        invalidate();
    }

    private static float clampAxis(float v) {
        return Math.max(-1f, Math.min(1f, v));
    }

    private void pressButton(Control c, boolean down) {
        if (down) {
            pressedButtons.add(c.buttonId);
            HudBridge.nativePostButton(c.buttonId, true);
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        }
        else {
            pressedButtons.remove(c.buttonId);
            HudBridge.nativePostButton(c.buttonId, false);
        }
        invalidate();
    }

    private void releasePointer(int pid) {
        String assigned = pointerControls.get(pid, "");
        pointerControls.delete(pid);
        if (assigned.isEmpty()) return;
        Control c = findControl(assigned);
        if (c != null && c != joystick) {
            pressButton(c, false);
        }
    }

    private Control findControl(String id) {
        for (Control c : controls) {
            if (c.id.equals(id)) return c;
        }
        return null;
    }

    // --- Edit mode -----------------------------------------------------------------

    private void enterEditMode() {
        if (!visible || editMode) return;
        Control hit = null;
        // Select the control under the original long-press position.
        for (int i = 0; i < pointerControls.size(); i++) {
            String assigned = pointerControls.valueAt(i);
            if (!assigned.isEmpty()) {
                hit = findControl(assigned);
                break;
            }
        }
        releaseAll();
        editMode = true;
        selectedControl = hit != null ? hit : controls.get(1);
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        log("edit mode entered (selected " + selectedControl.id + ")");
        invalidate();
    }

    private void exitEditMode() {
        if (!editMode) return;
        editMode = false;
        JSONObject json = toJSON();
        if (json != null) {
            controller.saveLayout(json);
        }
        invalidate();
    }

    private boolean handleEditTouch(MotionEvent event) {
        final int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                float x = event.getX();
                float y = event.getY();
                for (int i = 0; i < chipBounds.size(); i++) {
                    if (chipBounds.get(i).contains(x, y)) {
                        onChip(i);
                        return true;
                    }
                }
                Control c = controlAt(x, y);
                if (c != null) {
                    selectedControl = c;
                    pointerControls.put(event.getPointerId(0), c.id);
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                // Single-finger drag of the selected control in edit mode.
                if (selectedControl != null) {
                    float x = event.getX();
                    float y = event.getY();
                    selectedControl.nx = clamp01((x - insetL) / safeW());
                    selectedControl.ny = clamp01((y - insetT) / safeH());
                    invalidate();
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                pointerControls.clear();
                return true;
            default:
                return true;
        }
    }

    private void onChip(int index) {
        switch (CHIPS[index]) {
            case "Menor":
                if (selectedControl != null) {
                    selectedControl.nr = Math.max(0.035f, selectedControl.nr - 0.008f);
                }
                break;
            case "Maior":
                if (selectedControl != null) {
                    selectedControl.nr = Math.min(0.18f, selectedControl.nr + 0.008f);
                }
                break;
            case "Opac-":
                opacity = clamp01(opacity - 0.05f);
                break;
            case "Opac+":
                opacity = clamp01(opacity + 0.05f);
                break;
            case "Contorno":
                lowContrast = !lowContrast;
                break;
            case "Concluir":
                exitEditMode();
                return;
        }
        invalidate();
    }

    // --- Drawing ---------------------------------------------------------------------

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!visible || getAlpha() <= 0.01f) return;

        int alpha = (int) (opacity * 255);
        float base = Math.min(safeW(), safeH());

        for (Control c : controls) {
            if (!c.interactive) continue;
            float x = cx(c);
            float y = cy(c);
            float r = radius(c);
            boolean pressed = c.buttonId >= 0 && pressedButtons.contains(c.buttonId);

            if (lowContrast) {
                strokePaint.setStrokeWidth(Math.max(2f, base * 0.006f));
                strokePaint.setAlpha(alpha);
                canvas.drawCircle(x, y, r, strokePaint);
            }
            else {
                fillPaint.setAlpha(pressed ? Math.min(255, alpha + 90) : alpha);
                canvas.drawCircle(x, y, r, fillPaint);
                strokePaint.setStrokeWidth(Math.max(2f, base * 0.004f));
                strokePaint.setAlpha(200);
                canvas.drawCircle(x, y, r, strokePaint);
            }

            if (c == joystick) {
                float knobR = r * 0.45f;
                knobPaint.setAlpha(Math.min(255, alpha + 60));
                canvas.drawCircle(x + joyKnobDx, y + joyKnobDy, knobR, knobPaint);
            }
            else if (!c.label.isEmpty()) {
                textPaint.setTextSize(r * 0.85f);
                textPaint.setAlpha(lowContrast ? alpha : 255);
                Paint.FontMetrics fm = textPaint.getFontMetrics();
                float textY = y - (fm.ascent + fm.descent) / 2f;
                canvas.drawText(c.label, x, textY, textPaint);
            }

            if (editMode && c == selectedControl) {
                strokePaint.setStrokeWidth(Math.max(3f, base * 0.008f));
                strokePaint.setAlpha(255);
                strokePaint.setColor(0xFFFFC857);
                canvas.drawCircle(x, y, r * 1.15f, strokePaint);
                strokePaint.setColor(Color.WHITE);
            }
        }

        if (editMode) {
            drawChips(canvas);
        }
    }

    private void drawChips(Canvas canvas) {
        chipBounds.clear();
        float base = Math.min(safeW(), safeH());
        float chipH = base * 0.07f;
        float textSize = chipH * 0.42f;
        textPaint.setTextSize(textSize);
        Paint.FontMetrics fm = textPaint.getFontMetrics();
        float x = insetL + safeW() * 0.5f;
        float totalW = 0f;
        float[] widths = new float[CHIPS.length];
        for (int i = 0; i < CHIPS.length; i++) {
            widths[i] = textPaint.measureText(CHIPS[i]) + chipH;
            totalW += widths[i];
        }
        float cx = x - totalW / 2f;
        float cy = insetT + base * 0.03f;
        for (int i = 0; i < CHIPS.length; i++) {
            RectF rect = new RectF(cx, cy, cx + widths[i], cy + chipH);
            chipBounds.add(rect);
            fillPaint.setAlpha(160);
            canvas.drawRoundRect(rect, chipH / 2f, chipH / 2f, fillPaint);
            textPaint.setAlpha(255);
            canvas.drawText(CHIPS[i], rect.centerX(),
                    cy + chipH / 2f - (fm.ascent + fm.descent) / 2f, textPaint);
            cx += widths[i] + chipH * 0.25f;
        }
    }

    private static void log(String message) {
        if (DEBUG) {
            Log.d(TAG, message);
        }
    }
}
