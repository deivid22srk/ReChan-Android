// touchcontrols.cpp — on-screen touch controls rendered in-engine.
#include "extra/touchcontrols.h"

#if defined(RC_PLATFORM_ANDROID)

#include "extra/touchhud.h"
#include "gen/display.h"
#include "gen/common.h"
#include "gen/time.h"
#include "pc/tim.h"
#include "pc/textmgr.h"
#include "pddi/gles/AndroidPlatform.h"
#include "pddi/pddidev.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace touchcontrols {

using touchhud::HudContext;

// ---------------------------------------------------------------------------
// Control definitions (normalized positions; radius relative to min(w,h))
// ---------------------------------------------------------------------------

struct Control {
    const char* id;
    const char* label;
    int buttonId;    // GamepadButton::* or -1 for joystick
    float nx, ny;    // normalized center (0..1)
    float nr;        // normalized radius
};

static const Control kControls[] = {
    {"joy", "",    -1,                         0.120f, 0.700f, 0.105f},
    {"A",   "A",   GamepadButton::A,           0.865f, 0.775f, 0.072f},
    {"B",   "B",   GamepadButton::B,           0.775f, 0.685f, 0.062f},
    {"X",   "X",   GamepadButton::X,           0.875f, 0.600f, 0.062f},
    {"Y",   "Y",   GamepadButton::Y,           0.965f, 0.690f, 0.062f},
    {"RB",  "R1",  GamepadButton::RightBumper, 0.950f, 0.520f, 0.050f},
    {"LB",  "L1",  GamepadButton::LeftBumper,  0.045f, 0.280f, 0.050f},
    {"ST",  "≡",   GamepadButton::Start,       0.500f, 0.940f, 0.042f},
};
static constexpr int32_t kControlCount = sizeof(kControls) / sizeof(kControls[0]);
static constexpr int32_t kJoyIdx = 0;

// ---------------------------------------------------------------------------
// Visual style — dark translucent fills + light outlines so the white labels
// stay readable on top of the game frame. Matches the look of common mobile
// touch overlays (PPSSPP / AetherSX2 style) and avoids the previous issue
// where the white "Pular" / A / B / X / Y labels were camouflaged against a
// white circle background and looked like they were rendered behind.
// ---------------------------------------------------------------------------

struct TouchColor {
    u8 r, g, b, a;
};

static constexpr TouchColor kFill         = {  30,  30,  46, 175 }; // dark blue-gray, semi-transparent
static constexpr TouchColor kFillPressed  = {  60,  60,  90, 220 }; // brighter when held
static constexpr TouchColor kRing          = { 220, 220, 230, 230 }; // light outline
static constexpr TouchColor kRingJoystick  = { 200, 200, 215, 200 };
static constexpr TouchColor kKnob          = { 235, 235, 245, 230 };
static constexpr TouchColor kText          = { 255, 255, 255, 255 };
static constexpr TouchColor kSkipFill      = {  30,  30,  46, 200 };
static constexpr TouchColor kSkipRing      = { 220, 220, 230, 235 };
static constexpr TouchColor kSkipText      = { 255, 255, 255, 255 };

// Scale a color's alpha channel by the current HUD fade.
static inline u8 FadeAlpha(u8 a, float fade) {
    const int v = static_cast<int>(a) * static_cast<int>(fade * 255.0f + 0.5f) / 255;
    return static_cast<u8>(std::min(255, std::max(0, v)));
}

// Thin wrappers that pass all positional args to ScreenDraw's circle helpers
// so the color lands in the (r,g,b,a) tail, not in the UV slots. The previous
// code called DrawFilledCircle(x, y, r, r, R, G, B, A) — those trailing 4
// args matched u0/v0/u1/v1, leaving the color at its default (255,255,255,255),
// which is why the "Pular" / A / B / X / Y labels were camouflaged against an
// opaque white circle and looked like they were rendered behind the button.
static inline void FillCircle(float x, float y, float rx, float ry,
                             const TouchColor& c) {
    ScreenDraw::DrawFilledCircle(x, y, rx, ry,
                                 0.0f, 0.0f, 1.0f, 1.0f, 32,
                                 c.r, c.g, c.b, c.a);
}

static inline void RingCircle(float x, float y, float rx, float ry,
                              float thickness, const TouchColor& c) {
    ScreenDraw::DrawCircle(x, y, rx, ry, thickness,
                           0.0f, 0.0f, 1.0f, 1.0f, 32,
                           c.r, c.g, c.b, c.a);
}

// ---------------------------------------------------------------------------
// Runtime layout
// ---------------------------------------------------------------------------

static float s_screenW = 0.0f;
static float s_screenH = 0.0f;
static float s_cx[kControlCount];
static float s_cy[kControlCount];
static float s_cr[kControlCount];  // touch reach (pixels)
static float s_joyBaseR = 0.0f;    // joystick base radius (pixels)

static void RecomputeLayout() {
    const f32 w = SCREEN_WIDTH;
    const f32 h = SCREEN_HEIGHT;
    if (w == s_screenW && h == s_screenH) return;
    s_screenW = w;
    s_screenH = h;
    const f32 base = std::min(w, h);
    for (int32_t i = 0; i < kControlCount; ++i) {
        s_cx[i] = kControls[i].nx * w;
        s_cy[i] = kControls[i].ny * h;
        s_cr[i] = kControls[i].nr * base * 1.35f;
    }
    s_joyBaseR = kControls[kJoyIdx].nr * base;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static bool s_visible = false;
static float s_alpha = 0.0f;
static bool s_btnDown[kControlCount] = {};
static int32_t s_btnSlot[kControlCount]; // touch slot per control, -1 = none
static int32_t s_joySlot = -1;           // touch slot dragging joystick
static float s_knobX = 0.0f;
static float s_knobY = 0.0f;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int32_t FindControlAt(float x, float y) {
    for (int32_t i = 0; i < kControlCount; ++i) {
        const float dx = x - s_cx[i];
        const float dy = y - s_cy[i];
        if (dx * dx + dy * dy <= s_cr[i] * s_cr[i]) return i;
    }
    return -1;
}

static void ResetAll() {
    for (int32_t i = 0; i < kControlCount; ++i) {
        if (kControls[i].buttonId >= 0 && s_btnDown[i]) {
            androidbridge::PostGamepadButton(kControls[i].buttonId, false);
            s_btnDown[i] = false;
            s_btnSlot[i] = -1;
        }
    }
    if (s_joySlot >= 0) {
        androidbridge::PostGamepadAxis(GamepadAxis::LeftX, 0.0f);
        androidbridge::PostGamepadAxis(GamepadAxis::LeftY, 0.0f);
        s_joySlot = -1;
        s_knobX = s_knobY = 0.0f;
    }
}

// Returns whether a control is part of the visible set for the given context.
//   - OnFoot   : full controller (joy + A/B/X/Y + LB/RB + Start)
//   - Climbing : reduced set — joystick + A only
//   - Menu     : navigation set — joystick (d-pad style) + A (confirm) +
//                B (back) + Start (pause/title-confirm)
static bool IsControlInteractive(int32_t ci, HudContext ctx) {
    if (ci == kJoyIdx) return true;

    switch (ctx) {
        case HudContext::OnFoot:
            return true;
        case HudContext::Climbing:
            return std::strcmp(kControls[ci].id, "A") == 0;
        case HudContext::Menu:
            return std::strcmp(kControls[ci].id, "A") == 0
                || std::strcmp(kControls[ci].id, "B") == 0
                || std::strcmp(kControls[ci].id, "ST") == 0;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Update() {
    RecomputeLayout();

    const HudContext ctx = touchhud::GetContext();
    const bool wantVisible = (ctx == HudContext::OnFoot
                              || ctx == HudContext::Climbing
                              || ctx == HudContext::Menu);
    if (wantVisible && !s_visible) {
        ResetAll();
    }
    s_visible = wantVisible;

    // Fade
    const f32 dt = g_time ? g_time->GetDeltaTime() : (1.0f / 30.0f);
    const f32 fRate = 4.0f * dt;
    if (s_visible && s_alpha < 1.0f) {
        s_alpha += fRate;
        if (s_alpha > 1.0f) s_alpha = 1.0f;
    }
    else if (!s_visible && s_alpha > 0.0f) {
        s_alpha -= fRate;
        if (s_alpha < 0.0f) { s_alpha = 0.0f; ResetAll(); }
    }

    if (!s_visible && s_alpha <= 0.0f) return;

    androidbridge::TouchPoint pts[androidbridge::kMaxTouchPoints];
    const int32_t ptCount = androidbridge::LoadTouchPoints(pts, androidbridge::kMaxTouchPoints);

    // 1) Release any button whose touch slot is gone.
    for (int32_t ci = 0; ci < kControlCount; ++ci) {
        if (kControls[ci].buttonId < 0 || !s_btnDown[ci]) continue;
        const int32_t slot = s_btnSlot[ci];
        const bool stillHeld = (slot >= 0 && slot < ptCount && pts[slot].active);
        if (!stillHeld) {
            s_btnDown[ci] = false;
            s_btnSlot[ci] = -1;
            androidbridge::PostGamepadButton(kControls[ci].buttonId, false);
        }
    }

    // Release joystick if its slot is gone.
    if (s_joySlot >= 0 && !(s_joySlot < ptCount && pts[s_joySlot].active)) {
        androidbridge::PostGamepadAxis(GamepadAxis::LeftX, 0.0f);
        androidbridge::PostGamepadAxis(GamepadAxis::LeftY, 0.0f);
        s_joySlot = -1;
        s_knobX = s_knobY = 0.0f;
    }

    // 2) Assign/update active pointers.
    bool anyActive = false;
    for (int32_t pi = 0; pi < ptCount; ++pi) {
        if (!pts[pi].active) continue;

        // Joystick follow
        if (s_joySlot == pi) {
            const float dx = pts[pi].x - s_cx[kJoyIdx];
            const float dy = pts[pi].y - s_cy[kJoyIdx];
            const float r = s_joyBaseR;
            float mag = sqrtf(dx * dx + dy * dy);
            float nx = dx, ny = dy;
            if (mag > r) { nx = dx / mag * r; ny = dy / mag * r; }
            s_knobX = nx / r;
            s_knobY = ny / r;
            androidbridge::PostGamepadAxis(GamepadAxis::LeftX, s_knobX);
            androidbridge::PostGamepadAxis(GamepadAxis::LeftY, s_knobY);
            anyActive = true;
            continue;
        }

        // Already holding a button with this pointer?
        bool handled = false;
        for (int32_t ci = 0; ci < kControlCount; ++ci) {
            if (kControls[ci].buttonId >= 0 && s_btnDown[ci] && s_btnSlot[ci] == pi) {
                handled = true;
                anyActive = true;
                break;
            }
        }
        if (handled) continue;

        // Hit-test a new control.
        const int32_t hit = FindControlAt(pts[pi].x, pts[pi].y);
        if (hit < 0 || !IsControlInteractive(hit, ctx)) continue;

        if (kControls[hit].buttonId >= 0) {
            s_btnDown[hit] = true;
            s_btnSlot[hit] = pi;
            androidbridge::PostGamepadButton(kControls[hit].buttonId, true);
            anyActive = true;
        }
        else if (hit == kJoyIdx && s_joySlot == -1) {
            s_joySlot = pi;
            const float dx = pts[pi].x - s_cx[kJoyIdx];
            const float dy = pts[pi].y - s_cy[kJoyIdx];
            s_knobX = dx / s_joyBaseR;
            s_knobY = dy / s_joyBaseR;
            if (s_knobX > 1.0f) s_knobX = 1.0f;
            if (s_knobX < -1.0f) s_knobX = -1.0f;
            if (s_knobY > 1.0f) s_knobY = 1.0f;
            if (s_knobY < -1.0f) s_knobY = -1.0f;
            androidbridge::PostGamepadAxis(GamepadAxis::LeftX, s_knobX);
            androidbridge::PostGamepadAxis(GamepadAxis::LeftY, s_knobY);
            anyActive = true;
        }
    }

    if (anyActive) {
        androidbridge::PostGamepadConnected(true);
    }
}

void Render() {
    if (s_alpha <= 0.01f) return;
    RecomputeLayout();

    ScreenDraw::Batch batch;
    const f32 w = SCREEN_WIDTH;
    const f32 h = SCREEN_HEIGHT;
    const f32 base = std::min(w, h);

    const HudContext ctx = touchhud::GetContext();

    TextManager* tm = g_textManager;
    if (tm) {
        tm->SetFontByName("Menu");
        tm->SetColor(kText.r, kText.g, kText.b, kText.a);
        tm->SetPromptsEnabled(true);
    }

    // Pass 1: draw every visible control's background (filled circle + ring
    // + joystick knob). Drawing all circles first avoids per-button texture
    // flushes breaking the per-control draw order, and gives the text pass a
    // clean slate to render on top of.
    for (int32_t i = 0; i < kControlCount; ++i) {
        if (!IsControlInteractive(i, ctx)) continue;

        const float x = s_cx[i];
        const float y = s_cy[i];
        const float r = kControls[i].nr * base;

        if (kControls[i].buttonId >= 0) {
            const bool pressed = s_btnDown[i];
            TouchColor fill = pressed ? kFillPressed : kFill;
            fill.a = FadeAlpha(fill.a, s_alpha);
            TouchColor ring = kRing;
            ring.a = FadeAlpha(ring.a, s_alpha);
            FillCircle(x, y, r, r, fill);
            RingCircle(x, y, r, r, std::max(2.0f, r * 0.06f), ring);
        }
        else {
            // Joystick base + knob
            TouchColor fill = kFill;
            fill.a = FadeAlpha(fill.a, s_alpha);
            TouchColor ring = kRingJoystick;
            ring.a = FadeAlpha(ring.a, s_alpha);
            TouchColor knob = kKnob;
            knob.a = FadeAlpha(knob.a, s_alpha);

            FillCircle(x, y, r, r, fill);
            RingCircle(x, y, r, r, std::max(2.0f, r * 0.04f), ring);
            const float knobR = r * 0.45f;
            FillCircle(x + s_knobX * r, y + s_knobY * r, knobR, knobR, knob);
        }
    }

    // Pass 2: render labels on top of the circles. The text backend uses a
    // different texture (the font atlas), which causes the pending circle
    // batch to flush before the glyphs queue — so labels always land on top
    // of their corresponding button.
    if (tm) {
        for (int32_t i = 0; i < kControlCount; ++i) {
            if (!IsControlInteractive(i, ctx)) continue;
            if (kControls[i].buttonId < 0) continue;
            if (!kControls[i].label || kControls[i].label[0] == '\0') continue;

            const float x = s_cx[i];
            const float y = s_cy[i];
            const float r = kControls[i].nr * base;

            tm->SetAlignment(TextAlign_Center);
            const f32 textScale = (r * 1.4f) / 48.0f;
            tm->SetScale(SCREEN_SCALE_Y(textScale), SCREEN_SCALE_Y(textScale));
            const TextBounds b = tm->MeasureString(kControls[i].label);
            tm->PrintString(kControls[i].label, x, y - b.height / 2.0f);
        }
    }
}

void Reset() {
    ResetAll();
    s_visible = false;
    s_alpha = 0.0f;
}

// ---------------------------------------------------------------------------
// Movie / cutscene skip
// ---------------------------------------------------------------------------

static void DoSkip() {
    androidbridge::PostGamepadConnected(true);
    androidbridge::PostGamepadButton(GamepadButton::Start, true);
}

void UpdateMovieSkip() {
    RecomputeLayout();

    androidbridge::TouchPoint pts[androidbridge::kMaxTouchPoints];
    const int32_t ptCount = androidbridge::LoadTouchPoints(pts, androidbridge::kMaxTouchPoints);

    const float skipX = SCREEN_WIDTH * 0.90f;
    const float skipY = SCREEN_HEIGHT * 0.05f;
    const float skipR = std::min(SCREEN_WIDTH, SCREEN_HEIGHT) * 0.055f;

    for (int32_t i = 0; i < ptCount; ++i) {
        if (!pts[i].active) continue;
        const float dx = pts[i].x - skipX;
        const float dy = pts[i].y - skipY;
        if (dx * dx + dy * dy <= skipR * skipR * 2.0f) {
            DoSkip();
            return;
        }
    }
}

void RenderMovieSkip() {
    ScreenDraw::Batch batch;

    const float skipX = SCREEN_WIDTH * 0.90f;
    const float skipY = SCREEN_HEIGHT * 0.05f;
    const float r = std::min(SCREEN_WIDTH, SCREEN_HEIGHT) * 0.055f;

    // Pass 1: dark filled circle + light ring on top of the movie frame.
    // Uses the corrected color path: alpha lands in the color channel, not
    // in the UV slot, so the "Pular" label can be read on top of the circle.
    FillCircle(skipX, skipY, r, r, kSkipFill);
    RingCircle(skipX, skipY, r, r, std::max(2.0f, r * 0.06f), kSkipRing);

    // Pass 2: "Pular" label on top of the circle. The text backend uses the
    // font atlas texture, which forces the pending circle batch to flush
    // before the glyphs are queued — so the label is guaranteed to render
    // on top of the dark fill, not behind it.
    if (g_textManager) {
        g_textManager->SetFontByName("Menu");
        g_textManager->SetColor(kSkipText.r, kSkipText.g, kSkipText.b, kSkipText.a);
        g_textManager->SetPromptsEnabled(true);
        g_textManager->SetAlignment(TextAlign_Center);
        const f32 textScale = (r * 1.6f) / 48.0f;
        g_textManager->SetScale(SCREEN_SCALE_Y(textScale), SCREEN_SCALE_Y(textScale));
        const char* skipLabel = "Pular";
        const TextBounds b = g_textManager->MeasureString(skipLabel);
        g_textManager->PrintString(skipLabel, skipX, skipY - b.height / 2.0f);
    }
}

} // namespace touchcontrols

#endif // RC_PLATFORM_ANDROID
