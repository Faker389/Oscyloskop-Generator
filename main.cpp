#include "engine.h"
#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>
#include <cstring>

struct Hit { int id; Rectangle r; float rad; bool circ; };
static Hit gHits[200];
static int gHitN = 0;
static int gHeld = 0;
static int gNudgeN = 0;
static void ClearHits() { gHitN = 0; gNudgeN = 0; }
static void AddR(int id, Rectangle r)
{
    if (gHitN < 200) gHits[gHitN++] = { id, r, 0, false };
}
static void AddC(int id, Vector2 c, float rad)
{
    if (gHitN < 200) gHits[gHitN++] = { id, { c.x - rad, c.y - rad, rad * 2, rad * 2 }, rad, true };
}
static int PickHit(Vector2 p)
{
    for (int i = gHitN - 1; i >= 0; --i) {
        if (gHits[i].circ) {
            Vector2 c{ gHits[i].r.x + gHits[i].rad, gHits[i].r.y + gHits[i].rad };
            if (CheckCollisionPointCircle(p, c, gHits[i].rad)) return gHits[i].id;
        } else if (CheckCollisionPointRec(p, gHits[i].r)) return gHits[i].id;
    }
    return 0;
}
static bool IsKnob(int id)
{
    return id == S_MKNOB || id == S_VPOS || id == S_VSCALE || id == S_HPOS || id == S_HSCALE ||
           id == S_TLEV || id == G_KNOB;
}

// -----------------------------------------------------------------------------
// Colors matching the photos
// -----------------------------------------------------------------------------
static const Color SCOPE_BODY     = { 214, 205, 184, 255 };
static const Color SCOPE_BODY_DK  = { 190, 180, 158, 255 };
static const Color SCOPE_BODY_LT  = { 232, 224, 205, 255 };
static const Color SCOPE_TEAL     = {  70, 130, 140, 255 };
static const Color SCOPE_GREEN_BTN= { 146, 186, 140, 255 };
static const Color SCOPE_SOFTKEY  = { 168, 184, 190, 255 };
static const Color SCOPE_KNOB     = { 210, 202, 180, 255 };
static const Color SCOPE_KNOB_DK  = { 150, 142, 122, 255 };

static const Color GEN_BODY       = { 228, 230, 234, 255 };
static const Color GEN_BODY_DK    = { 196, 200, 208, 255 };
static const Color GEN_BODY_LT    = { 244, 246, 248, 255 };
static const Color GEN_SOFTKEY    = { 132, 176, 204, 255 };
static const Color GEN_SINE       = {  90, 196, 110, 255 };
static const Color GEN_YELLOW     = { 230, 200,  55, 255 };
static const Color GEN_BLUE       = {  70, 140, 205, 255 };
static const Color GEN_KEY        = { 248, 248, 250, 255 };

static const Color METAL          = { 170, 174, 180, 255 };
static const Color METAL_LT       = { 220, 222, 226, 255 };
static const Color METAL_DK       = {  90,  94, 100, 255 };
static const Color LCD_BG         = {   8,  10,  14, 255 };
static const Color LCD_BEZEL      = {  28,  30,  34, 255 };
static const Color LABEL_DK       = {  40,  42,  46, 255 };

// -----------------------------------------------------------------------------
// Drawing helpers
// -----------------------------------------------------------------------------
static void DrawCenteredText(const char *text, Rectangle r, int fontSize, Color color)
{
    int w = MeasureText(text, fontSize);
    float x = r.x + (r.width - (float)w) * 0.5f;
    float y = r.y + (r.height - (float)fontSize) * 0.5f;
    DrawText(text, (int)x, (int)(y + 1), fontSize, color);
}

static void DrawRaisedRect(Rectangle r, Color face, float radius)
{
    DrawRectangleRounded((Rectangle){ r.x + 1, r.y + 2, r.width, r.height }, radius / r.height, 6,
                         ColorBrightness(face, -0.25f));
    DrawRectangleRounded(r, radius / ((r.height > 0) ? r.height : 1), 6, face);
    DrawRectangleRoundedLinesEx(r, radius / ((r.height > 0) ? r.height : 1), 6, 1.0f,
                                ColorBrightness(face, 0.18f));
}

static void DrawSunkenRect(Rectangle r, Color face, float radius)
{
    DrawRectangleRounded(r, radius / ((r.height > 0) ? r.height : 1), 6, ColorBrightness(face, -0.35f));
    DrawRectangleRounded((Rectangle){ r.x + 1, r.y + 1, r.width - 2, r.height - 2 },
                         radius / ((r.height > 2) ? (r.height - 2) : 1), 6, face);
}

static void DrawPanelBox(Rectangle r, const char *title)
{
    DrawRectangleRoundedLinesEx(r, 0.03f, 6, 1.4f, SCOPE_TEAL);
    if (title && title[0])
    {
        int fs = 10;
        int tw = MeasureText(title, fs);
        DrawText(title, (int)(r.x + (r.width - tw) * 0.5f), (int)(r.y + 3), fs, SCOPE_TEAL);
    }
}

static void DrawBevelButton(Rectangle r, const char *label, Color face, Color text, int fontSize)
{
    DrawRaisedRect(r, face, 3.0f);
    DrawRectangleRoundedLinesEx(r, 0.12f, 4, 1.0f, ColorBrightness(face, -0.35f));
    if (label && label[0]) DrawCenteredText(label, r, fontSize, text);
}

static void Btn(int id, Rectangle r, const char *label, Color face, Color text, int fontSize)
{
    if (gHeld == id) face = ColorBrightness(face, -0.18f);
    DrawBevelButton(r, label, face, text, fontSize);
    AddR(id, r);
}

static void DrawRoundBtn(Vector2 c, float rad, const char *label, Color face, int fontSize)
{
    DrawCircle((int)c.x, (int)(c.y + 2), rad, ColorBrightness(face, -0.3f));
    DrawCircle((int)c.x, (int)c.y, rad, face);
    DrawCircleLines((int)c.x, (int)c.y, rad, ColorBrightness(face, -0.4f));
    if (label)
    {
        int w = MeasureText(label, fontSize);
        DrawText(label, (int)(c.x - w * 0.5f), (int)(c.y - fontSize * 0.5f), fontSize, LABEL_DK);
    }
}

static float gSpin[7];

static int SpinSlot(int id)
{
    switch (id) {
    case S_MKNOB: return 0;
    case S_VPOS: return 1;
    case S_VSCALE: return 2;
    case S_HPOS: return 3;
    case S_HSCALE: return 4;
    case S_TLEV: return 5;
    case G_KNOB: return 6;
    default: return -1;
    }
}

static void TurnKnob(int id, int steps)
{
    int slot = SpinSlot(id);
    if (slot >= 0 && steps) gSpin[slot] += steps * 18.0f;
    AppTurn(id, steps);
}

static void DrawKnob(Vector2 c, float rad, float angleDeg, Color body, bool large)
{
    DrawCircle((int)c.x, (int)(c.y + 3), rad + 1, Fade(BLACK, 0.25f));
    DrawCircle((int)c.x, (int)c.y, rad + 2, ColorBrightness(body, -0.35f));
    DrawCircle((int)c.x, (int)c.y, rad, body);
    DrawCircleGradient((int)c.x - (int)(rad * 0.2f), (int)c.y - (int)(rad * 0.25f),
                       rad * 0.85f, ColorBrightness(body, 0.25f), Fade(body, 0.0f));

    float a0 = (angleDeg - 90.0f) * DEG2RAD;
    int ridges = large ? 18 : 14;
    for (int i = 0; i < ridges; i++)
    {
        float a = a0 + i * (2.0f * PI / ridges);
        Vector2 p1 = { c.x + cosf(a) * (rad - 1), c.y + sinf(a) * (rad - 1) };
        Vector2 p2 = { c.x + cosf(a) * (rad + 1.5f), c.y + sinf(a) * (rad + 1.5f) };
        DrawLineEx(p1, p2, 1.5f, ColorBrightness(body, -0.45f));
    }

    Vector2 tip = { c.x + cosf(a0) * (rad * 0.72f), c.y + sinf(a0) * (rad * 0.72f) };
    DrawLineEx(c, tip, 2.4f, ColorBrightness(body, -0.55f));
    DrawCircle((int)c.x, (int)c.y, rad * 0.22f, ColorBrightness(body, -0.15f));
    DrawCircle((int)c.x, (int)c.y, rad * 0.12f, ColorBrightness(body, 0.2f));
}

static void DrawKnobWithCaption(Vector2 c, float rad, float angle, Color body, bool large,
                                const char *above, const char *below)
{
    if (above)
    {
        int w = MeasureText(above, 9);
        DrawText(above, (int)(c.x - w * 0.5f), (int)(c.y - rad - 14), 9, SCOPE_TEAL);
    }
    DrawKnob(c, rad, angle, body, large);
    if (below)
    {
        int w = MeasureText(below, 9);
        DrawText(below, (int)(c.x - w * 0.5f), (int)(c.y + rad + 4), 9, SCOPE_TEAL);
    }
}

struct Nudge { Rectangle r; int id; int dir; };
static Nudge gNudges[12];
static void AddNudge(int id, Rectangle r, int dir)
{
    if (gNudgeN < 12) gNudges[gNudgeN++] = { r, id, dir };
}
static int PickNudge(Vector2 p)
{
    for (int i = gNudgeN - 1; i >= 0; --i)
        if (CheckCollisionPointRec(p, gNudges[i].r)) return i;
    return -1;
}

static void DrawArrowsUD(Vector2 c, float rad, int id)
{
    float x = c.x + rad + 6;
    DrawTriangle((Vector2){ x, c.y - 8 }, (Vector2){ x - 4, c.y - 2 }, (Vector2){ x + 4, c.y - 2 }, SCOPE_TEAL);
    DrawTriangle((Vector2){ x, c.y + 8 }, (Vector2){ x - 4, c.y + 2 }, (Vector2){ x + 4, c.y + 2 }, SCOPE_TEAL);
    AddNudge(id, (Rectangle){ x - 8, c.y - 16, 16, 14 }, +1);
    AddNudge(id, (Rectangle){ x - 8, c.y + 2, 16, 14 }, -1);
}

static void DrawArrowsLR(Vector2 c, float rad, int id)
{
    float y = c.y + rad + 2;
    DrawTriangle((Vector2){ c.x - 10, y }, (Vector2){ c.x - 4, y - 4 }, (Vector2){ c.x - 4, y + 4 }, SCOPE_TEAL);
    DrawTriangle((Vector2){ c.x + 10, y }, (Vector2){ c.x + 4, y - 4 }, (Vector2){ c.x + 4, y + 4 }, SCOPE_TEAL);
    AddNudge(id, (Rectangle){ c.x - 18, y - 8, 14, 16 }, -1);
    AddNudge(id, (Rectangle){ c.x + 4, y - 8, 14, 16 }, +1);
}

static void DrawBNC(Vector2 c, float rad, Color ring)
{
    DrawCircle((int)c.x, (int)(c.y + 2), rad + 3, Fade(BLACK, 0.25f));
    DrawCircle((int)c.x, (int)c.y, rad + 3, ring);
    DrawCircle((int)c.x, (int)c.y, rad + 1, METAL);
    DrawCircle((int)c.x, (int)c.y, rad - 2, METAL_DK);
    DrawCircle((int)c.x, (int)c.y, rad - 5, BLACK);
    DrawCircle((int)c.x, (int)c.y, 3.0f, METAL_LT);
    // Bayonet nubs
    DrawCircle((int)(c.x - rad - 1), (int)c.y, 2.4f, METAL);
    DrawCircle((int)(c.x + rad + 1), (int)c.y, 2.4f, METAL);
}

static void DrawUSBPort(Rectangle r)
{
    DrawSunkenRect(r, Color{ 40, 42, 48, 255 }, 2.0f);
    Rectangle inner = { r.x + 4, r.y + r.height * 0.28f, r.width - 8, r.height * 0.44f };
    DrawRectangleRec(inner, Color{ 20, 20, 22, 255 });
    DrawRectangleLinesEx(inner, 1, METAL);
    // USB trident
    float cx = r.x + r.width * 0.5f;
    float cy = r.y + r.height * 0.5f;
    DrawCircle((int)cx, (int)(cy + 4), 1.6f, METAL_LT);
    DrawLineEx((Vector2){ cx, cy + 4 }, (Vector2){ cx, cy - 5 }, 1.2f, METAL_LT);
}

static void DrawMiniSine(Rectangle r, Color col)
{
    Vector2 prev = { 0 };
    int n = (int)r.width;
    for (int i = 0; i <= n; i++)
    {
        float t = (float)i / (float)n;
        Vector2 p = { r.x + t * r.width,
                      r.y + r.height * 0.5f - sinf(t * 2.0f * PI) * (r.height * 0.42f) };
        if (i > 0) DrawLineEx(prev, p, 1.6f, col);
        prev = p;
    }
}

static void DrawMiniSquare(Rectangle r, Color col)
{
    float y0 = r.y + r.height * 0.15f;
    float y1 = r.y + r.height * 0.85f;
    float x0 = r.x + 2, x1 = r.x + r.width * 0.5f, x2 = r.x + r.width - 2;
    DrawLineEx((Vector2){ x0, y1 }, (Vector2){ x0, y0 }, 1.6f, col);
    DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y0 }, 1.6f, col);
    DrawLineEx((Vector2){ x1, y0 }, (Vector2){ x1, y1 }, 1.6f, col);
    DrawLineEx((Vector2){ x1, y1 }, (Vector2){ x2, y1 }, 1.6f, col);
}

static void DrawMiniRamp(Rectangle r, Color col)
{
    float y0 = r.y + r.height * 0.85f;
    float y1 = r.y + r.height * 0.15f;
    DrawLineEx((Vector2){ r.x + 2, y0 }, (Vector2){ r.x + r.width - 4, y1 }, 1.6f, col);
    DrawLineEx((Vector2){ r.x + r.width - 4, y1 }, (Vector2){ r.x + r.width - 4, y0 }, 1.6f, col);
}

static void DrawMiniPulse(Rectangle r, Color col)
{
    float y0 = r.y + r.height * 0.8f;
    float y1 = r.y + r.height * 0.2f;
    float x0 = r.x + 3;
    float x1 = r.x + r.width * 0.28f;
    float x2 = r.x + r.width * 0.5f;
    float x3 = r.x + r.width - 3;
    DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y0 }, 1.6f, col);
    DrawLineEx((Vector2){ x1, y0 }, (Vector2){ x1, y1 }, 1.6f, col);
    DrawLineEx((Vector2){ x1, y1 }, (Vector2){ x2, y1 }, 1.6f, col);
    DrawLineEx((Vector2){ x2, y1 }, (Vector2){ x2, y0 }, 1.6f, col);
    DrawLineEx((Vector2){ x2, y0 }, (Vector2){ x3, y0 }, 1.6f, col);
}

static void DrawMiniNoise(Rectangle r, Color col)
{
    Vector2 prev = { 0 };
    int n = (int)r.width;
    for (int i = 0; i <= n; i++)
    {
        float t = (float)i / (float)n;
        float nse = sinf(t * 37.0f) * 0.4f + sinf(t * 73.0f) * 0.3f + sinf(t * 17.0f) * 0.3f;
        Vector2 p = { r.x + t * r.width, r.y + r.height * 0.5f - nse * (r.height * 0.45f) };
        if (i > 0) DrawLineEx(prev, p, 1.4f, col);
        prev = p;
    }
}

static void DrawMiniArb(Rectangle r, Color col)
{
    Vector2 pts[6] = {
        { r.x + 2, r.y + r.height * 0.7f },
        { r.x + r.width * 0.25f, r.y + r.height * 0.2f },
        { r.x + r.width * 0.45f, r.y + r.height * 0.55f },
        { r.x + r.width * 0.65f, r.y + r.height * 0.25f },
        { r.x + r.width * 0.8f,  r.y + r.height * 0.75f },
        { r.x + r.width - 2,     r.y + r.height * 0.4f },
    };
    for (int i = 1; i < 6; i++) DrawLineEx(pts[i - 1], pts[i], 1.6f, col);
}

static void DrawWarningTriangle(Vector2 c, float s)
{
    DrawTriangle((Vector2){ c.x, c.y - s },
                 (Vector2){ c.x - s * 0.9f, c.y + s * 0.7f },
                 (Vector2){ c.x + s * 0.9f, c.y + s * 0.7f },
                 Color{ 210, 170, 40, 255 });
    DrawText("!", (int)(c.x - 2), (int)(c.y - s * 0.45f), 8, BLACK);
}

static void DrawFoot(float x, float y, float w, float h)
{
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.2f, 4, Color{ 70, 68, 64, 255 });
}
static void DrawShapeIn(Rectangle r, int ch, Color col)
{
    const GenCh &c = app.gen.ch[ch];
    int n = (int)r.width;
    if (n < 2) return;
    Vector2 prev{};
    bool pen = false;
    for (int i = 0; i <= n; i++) {
        double p = (double)i / (double)n;
        double s = c.dc ? 0.0 : ShapeOf(c, p + c.phase / 360.0);
        if (ch == 0 && app.gen.mode == 1 && app.gen.modType == 0 && app.gen.modSource == 0)
            s *= 1.0 + (app.gen.amDepth / 100.0) * std::sin(2.0 * PI * p);
        if (s > 1.35) s = 1.35;
        if (s < -1.35) s = -1.35;
        float y = r.y + r.height * 0.5f - (float)s * r.height * 0.40f;
        Vector2 cur{ r.x + (float)i, y };
        if (pen) DrawLineEx(prev, cur, 1.5f, col);
        prev = cur;
        pen = true;
    }
}

static Camera2D cam = { { 0, 0 }, { 0, 0 }, 0.0f, 1.0f };
static Font gUi{};

static void DrawPl(const char *text, float x, float y, float size, Color c)
{
    if (gUi.texture.id) DrawTextEx(gUi, text, Vector2{ x, y }, size, 0.3f, c);
    else DrawText(text, (int)x, (int)y, (int)size, c);
}

static void BeginWorldScissor(float x, float y, float w, float h)
{
    Vector2 a = GetWorldToScreen2D(Vector2{ x, y }, cam);
    Vector2 b = GetWorldToScreen2D(Vector2{ x + w, y + h }, cam);
    float x0 = a.x < b.x ? a.x : b.x;
    float y0 = a.y < b.y ? a.y : b.y;
    float x1 = a.x > b.x ? a.x : b.x;
    float y1 = a.y > b.y ? a.y : b.y;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    if (x1 <= 0 || y1 <= 0 || x0 >= sw || y0 >= sh) {
        BeginScissorMode(0, 0, 0, 0);
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = (float)sw;
    if (y1 > sh) y1 = (float)sh;
    int iw = (int)std::ceil(x1) - (int)std::floor(x0);
    int ih = (int)std::ceil(y1) - (int)std::floor(y0);
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    BeginScissorMode((int)std::floor(x0), (int)std::floor(y0), iw, ih);
}

static float VoltY(int ch, float volts, float gy, float gh, double vdiv, double pos)
{
    float cy = gy + gh * 0.5f;
    float ppy = gh / 8.0f;
    return cy - ((volts / (float)vdiv) + (float)pos) * ppy;
}

static void DrawScopeScreen(Rectangle lcd)
{
    Scope &s = app.sc;
    DrawRectangleRounded((Rectangle){ lcd.x - 6, lcd.y - 6, lcd.width + 12, lcd.height + 12 }, 0.03f, 6, LCD_BEZEL);
    Color lcdBg = s.screenInv ? Color{ 214, 224, 210, 255 } : LCD_BG;
    DrawRectangleRec(lcd, lcdBg);

    const char *st = s.status == 2 ? "STOP" : s.status == 3 ? "WAIT" : s.status == 4 ? "TRIG'D" : s.trigMode == 0 ? "AUTO" : "RUN";
    DrawText("RIGOL", (int)(lcd.x + 6), (int)(lcd.y + 4), 12, Color{ 80, 200, 120, 255 });
    DrawText(st, (int)(lcd.x + 62), (int)(lcd.y + 4), 12, Color{ 80, 220, 120, 255 });

    char mb[48], nb[48];
    if (s.measV >= 0) {
        FormatMeasV(s.measSrc, s.measV, mb, 48);
        std::snprintf(nb, 48, "%s %s", MeasNameV(s.measV), mb);
        DrawText(nb, (int)(lcd.x + 120), (int)(lcd.y + 3), 11, Color{ 220, 210, 140, 255 });
    }
    if (s.measT >= 0) {
        FormatMeasT(s.measSrc, s.measT, mb, 48);
        std::snprintf(nb, 48, "%s %s", MeasNameT(s.measT), mb);
        DrawText(nb, (int)(lcd.x + 120), (int)(lcd.y + 16), 11, Color{ 180, 220, 255, 255 });
    }
    if (s.counterOn) {
        FormatMeasT(0, 0, mb, 48);
        std::snprintf(nb, 48, "Cnt %s", mb);
        DrawText(nb, (int)(lcd.x + 250), (int)(lcd.y + 4), 11, ORANGE);
    }
    if (s.curMode) {
        char ca[32], cb[32], cd[32];
        if (s.curType == 0) {
            FmtVolt(ca, 32, s.curA); FmtVolt(cb, 32, s.curB); FmtVolt(cd, 32, s.curA - s.curB);
            std::snprintf(nb, 48, "CurA %s", ca);
            DrawText(nb, (int)(lcd.x + 230), (int)(lcd.y + 3), 10, Color{ 200, 210, 180, 255 });
            std::snprintf(nb, 48, "CurB %s  dV %s", cb, cd);
            DrawText(nb, (int)(lcd.x + 230), (int)(lcd.y + 15), 10, Color{ 200, 210, 180, 255 });
        } else {
            double tdiv = s.delayOn ? s.delayDiv : s.tdiv;
            FmtTime(ca, 32, s.curA * tdiv); FmtTime(cb, 32, s.curB * tdiv);
            double dt = (s.curA - s.curB) * tdiv;
            FmtTime(cd, 32, dt);
            std::snprintf(nb, 48, "tA %s  tB %s", ca, cb);
            DrawText(nb, (int)(lcd.x + 200), (int)(lcd.y + 3), 10, Color{ 200, 210, 180, 255 });
            std::snprintf(nb, 48, "dt %s", cd);
            DrawText(nb, (int)(lcd.x + 200), (int)(lcd.y + 15), 10, Color{ 200, 210, 180, 255 });
        }
    }

    float gx = lcd.x + 14, gy = lcd.y + 32;
    float gw = lcd.width - 86, gh = lcd.height - 52;
    Color grid = s.screenInv ? Color{ 30, 90, 40, (unsigned char)(s.gridBright * 255) }
                             : Color{ 40, 90, 50, (unsigned char)(s.gridBright * 255) };
    if (s.grid == 0) {
        Color minor = Fade(grid, 0.85f);
        const float dot = 1.4f, gap = 2.2f, pitch = dot + gap;
        auto dottedV = [&](float x) {
            for (float y = gy; y < gy + gh; y += pitch)
                DrawLineEx((Vector2){ x, y }, (Vector2){ x, y + dot > gy + gh ? gy + gh : y + dot }, 1, minor);
        };
        auto dottedH = [&](float y) {
            for (float x = gx; x < gx + gw; x += pitch)
                DrawLineEx((Vector2){ x, y }, (Vector2){ x + dot > gx + gw ? gx + gw : x + dot, y }, 1, minor);
        };
        for (int i = 0; i < 10; i++)
            for (int k = 1; k <= 4; k++)
                dottedV(gx + (i + k / 5.0f) * (gw / 10.0f));
        for (int i = 0; i < 8; i++)
            for (int k = 1; k <= 4; k++)
                dottedH(gy + (i + k / 5.0f) * (gh / 8.0f));
        for (int i = 0; i <= 10; i++) {
            float x = gx + gw * i / 10.0f;
            DrawLineEx((Vector2){ x, gy }, (Vector2){ x, gy + gh }, 1, Fade(grid, 0.75f));
        }
        for (int i = 0; i <= 8; i++) {
            float y = gy + gh * i / 8.0f;
            DrawLineEx((Vector2){ gx, y }, (Vector2){ gx + gw, y }, 1, Fade(grid, 0.75f));
        }
    } else if (s.grid == 1) {
        DrawLineEx((Vector2){ gx, gy + gh * 0.5f }, (Vector2){ gx + gw, gy + gh * 0.5f }, 1, grid);
        DrawLineEx((Vector2){ gx + gw * 0.5f, gy }, (Vector2){ gx + gw * 0.5f, gy + gh }, 1, grid);
    }

    BeginWorldScissor(gx, gy, gw, gh);
    Color cols[2] = { Color{ 230, 200, 70, 255 }, Color{ 70, 210, 140, 255 } };
    cols[0].a = (unsigned char)(s.intensity * 255);
    cols[1].a = cols[0].a;
    if (s.timeBase == 1) {
        for (int i = 1; i < COLS; i++) {
            float x0 = gx + gw * 0.5f + (s.col[0][i - 1].mid / (float)s.ch[0].vdiv) * (gw / 10.0f);
            float x1 = gx + gw * 0.5f + (s.col[0][i].mid / (float)s.ch[0].vdiv) * (gw / 10.0f);
            float y0 = VoltY(1, s.col[1][i - 1].mid, gy, gh, s.ch[1].vdiv, s.ch[1].pos);
            float y1 = VoltY(1, s.col[1][i].mid, gy, gh, s.ch[1].vdiv, s.ch[1].pos);
            DrawCircleV((Vector2){ x1, y1 }, 1.3f, cols[0]);
            (void)x0; (void)y0;
        }
    }
    if (s.timeBase != 1 && s.persist && s.ghostN > 0) {
        int n = s.ghostN < 6 ? s.ghostN : 6;
        for (int g = 0; g < n; g++) {
            for (int c = 0; c < 2; c++) {
                if (!s.ch[c].on) continue;
                for (int i = 1; i < COLS; i++) {
                    float x0 = gx + gw * (i - 1) / (float)COLS;
                    float x1 = gx + gw * i / (float)COLS;
                    float y0 = VoltY(c, s.ghost[g][c][i - 1], gy, gh, s.ch[c].vdiv, s.ch[c].pos);
                    float y1 = VoltY(c, s.ghost[g][c][i], gy, gh, s.ch[c].vdiv, s.ch[c].pos);
                    DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.0f, Fade(cols[c], 0.25f));
                }
            }
        }
    }
    for (int c = 0; c < 2 && s.timeBase != 1; c++) {
        if (!s.ch[c].on) continue;
        for (int i = 1; i < COLS; i++) {
            float x0 = gx + gw * (i - 1) / (float)COLS;
            float x1 = gx + gw * i / (float)COLS;
            if (s.acq == 2) {
                float yLo = VoltY(c, s.col[c][i].lo, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
                float yHi = VoltY(c, s.col[c][i].hi, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
                DrawLineEx((Vector2){ x1, yLo }, (Vector2){ x1, yHi }, 1.2f, cols[c]);
            }
            float y0 = VoltY(c, s.col[c][i - 1].mid, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
            float y1 = VoltY(c, s.col[c][i].mid, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
            if (s.drawMode == 0) DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.6f, cols[c]);
            else DrawCircleV((Vector2){ x1, y1 }, 1.2f, cols[c]);
        }
    }
    if (s.timeBase != 1 && s.mathOn && s.mathOp != 3) {
        Color mc{ 190, 120, 255, (unsigned char)(s.intensity * 255) };
        for (int i = 1; i < COLS; i++) {
            float x0 = gx + gw * (i - 1) / (float)COLS;
            float x1 = gx + gw * i / (float)COLS;
            float y0 = VoltY(0, s.math[i - 1].mid, gy, gh, s.mathVdiv, s.mathPos);
            float y1 = VoltY(0, s.math[i].mid, gy, gh, s.mathVdiv, s.mathPos);
            DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.5f, mc);
        }
    }
    if (s.timeBase != 1 && s.mathOn && s.mathOp == 3 && s.fftOk) {
        Color mc{ 190, 120, 255, 255 };
        float fy = s.fftFull ? gy : gy + gh * 0.55f;
        float fh = s.fftFull ? gh : gh * 0.42f;
        for (int i = 1; i < 128; i++) {
            float x0 = gx + gw * (i - 1) / 128.0f;
            float x1 = gx + gw * i / 128.0f;
            float y0 = VoltY(0, s.fftMag[i - 1], fy, fh, s.fftDb, s.mathPos);
            float y1 = VoltY(0, s.fftMag[i], fy, fh, s.fftDb, s.mathPos);
            DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.4f, mc);
        }
    }
    if (s.timeBase != 1 && s.refOn) {
        for (int i = 1; i < COLS; i++) {
            float x0 = gx + gw * (i - 1) / (float)COLS;
            float x1 = gx + gw * i / (float)COLS;
            float y0 = VoltY(0, s.refV[i - 1], gy, gh, s.refVdiv, s.refPos);
            float y1 = VoltY(0, s.refV[i], gy, gh, s.refVdiv, s.refPos);
            DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.3f, Color{ 180, 200, 220, 255 });
        }
    }
    if (s.curMode >= 2) {
        int c = s.curSrc;
        Color cc[2] = { Color{ 255, 180, 60, 255 }, Color{ 255, 120, 60, 255 } };
        double curs[2] = { s.curA, s.curB };
        for (int k = 0; k < 2; k++) {
            float x = gx + gw * (float)((curs[k] + 5.0) / 10.0);
            int ix = (int)((curs[k] + 5.0) / 10.0 * (COLS - 1));
            if (ix < 0) ix = 0;
            if (ix >= COLS) ix = COLS - 1;
            float y = VoltY(c, s.col[c][ix].mid, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
            DrawLineEx((Vector2){ x, gy }, (Vector2){ x, gy + gh }, 1, cc[k]);
            DrawLineEx((Vector2){ gx, y }, (Vector2){ gx + gw, y }, 1, cc[k]);
        }
    } else if (s.curMode && s.curType == 0) {
        int c = s.curSrc;
        float yA = VoltY(c, (float)s.curA, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
        float yB = VoltY(c, (float)s.curB, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
        DrawLineEx((Vector2){ gx, yA }, (Vector2){ gx + gw, yA }, 1, Color{ 255, 180, 60, 255 });
        DrawLineEx((Vector2){ gx, yB }, (Vector2){ gx + gw, yB }, 1, Color{ 255, 120, 60, 255 });
    }
    if (s.curMode && s.curMode < 2 && s.curType == 1) {
        float xA = gx + gw * (float)((s.curA + 5.0) / 10.0);
        float xB = gx + gw * (float)((s.curB + 5.0) / 10.0);
        DrawLineEx((Vector2){ xA, gy }, (Vector2){ xA, gy + gh }, 1, Color{ 255, 180, 60, 255 });
        DrawLineEx((Vector2){ xB, gy }, (Vector2){ xB, gy + gh }, 1, Color{ 255, 120, 60, 255 });
    }
    EndScissorMode();

    for (int c = 0; c < 2; c++) {
        if (!s.ch[c].on) continue;
        float y = VoltY(c, 0, gy, gh, s.ch[c].vdiv, s.ch[c].pos);
        if (y < gy) y = gy;
        if (y > gy + gh - 10) y = gy + gh - 10;
        DrawText(c ? "2>" : "1>", (int)(lcd.x + 1), (int)(y - 5), 10, cols[c]);
    }
    float tx = gx + gw * (float)((s.hpos + 5.0) / 10.0);
    if (tx < gx) tx = gx;
    if (tx > gx + gw) tx = gx + gw;
    DrawTriangle((Vector2){ tx, gy }, (Vector2){ tx - 5, gy - 7 }, (Vector2){ tx + 5, gy - 7 }, ORANGE);

    if (s.dispAll) {
        char lines[20][24];
        static const char *nm[20] = {
            "Vpp", "Vmax", "Vmin", "Vavg", "Vrms", "Vamp", "Vtop", "Vbase", "Over", "Pre",
            "Freq", "Per", "Rise", "Fall", "+W", "-W", "+D", "-D", "DlyR", "DlyF"
        };
        FormatAllMeas(s.measSrc, lines);
        for (int i = 0; i < 10; i++) {
            char row[48];
            std::snprintf(row, 48, "%s %s", nm[i], lines[i]);
            DrawText(row, (int)(gx + 4), (int)(gy + 2 + i * 11), 10, Color{ 180, 210, 160, 255 });
            std::snprintf(row, 48, "%s %s", nm[i + 10], lines[i + 10]);
            DrawText(row, (int)(gx + 110), (int)(gy + 2 + i * 11), 10, Color{ 160, 200, 220, 255 });
        }
    }
    if (s.menuOn && s.page != SP_NONE) {
        float mx = lcd.x + lcd.width - 74;
        DrawRectangle((int)(mx - 4), (int)(lcd.y + 22), 76, (int)(lcd.height - 40), Color{ 0, 28, 16, 230 });
        for (int i = 0; i < 5; i++) {
            char a[32], b[32];
            bool hot = false;
            ScopeMenu(i, a, b, &hot);
            float y = lcd.y + 34 + i * 50;
            Color col = hot ? Color{ 255, 220, 80, 255 } : WHITE;
            if (a[0]) DrawText(a, (int)mx, (int)y, 10, col);
            if (b[0]) DrawText(b, (int)mx, (int)(y + 12), 10, Color{ 80, 230, 120, 255 });
        }
    }
    if (app.smsgT > 0) DrawText(app.smsg, (int)(lcd.x + 8), (int)(gy + gh - 14), 11, Color{ 255, 220, 120, 255 });

    char fb[32];
    const char *c0 = s.ch[0].coupling == 0 ? "DC" : s.ch[0].coupling == 1 ? "AC" : "GND";
    const char *c1 = s.ch[1].coupling == 0 ? "DC" : s.ch[1].coupling == 1 ? "AC" : "GND";
    FmtVolt(fb, 32, s.ch[0].vdiv);
    DrawText(TextFormat("CH1 %s %s%s", fb, c0, s.ch[0].bwLimit ? " B" : ""), (int)(lcd.x + 6), (int)(lcd.y + lcd.height - 16), 11, cols[0]);
    FmtVolt(fb, 32, s.ch[1].vdiv);
    DrawText(TextFormat("CH2 %s %s%s", fb, c1, s.ch[1].bwLimit ? " B" : ""), (int)(lcd.x + 130), (int)(lcd.y + lcd.height - 16), 11, cols[1]);
    FmtTime(fb, 32, s.delayOn ? s.delayDiv : s.tdiv);
    DrawText(TextFormat("T %s", fb), (int)(lcd.x + 214), (int)(lcd.y + lcd.height - 16), 11, ORANGE);
}

static void DrawGenValue(float x, float y, const char *k, const char *v, Color c, bool hot, int mark)
{
    DrawText(k, (int)x, (int)y, 11, Color{ 170, 180, 190, 255 });
    float vx = x + 58;
    if (!hot || mark < 0 || mark >= (int)std::strlen(v)) {
        DrawText(v, (int)vx, (int)y, 12, hot ? Color{ 255, 230, 80, 255 } : c);
        return;
    }
    char pre[48], one[2] = { v[mark], 0 }, post[48];
    std::snprintf(pre, sizeof(pre), "%.*s", mark, v);
    std::snprintf(post, sizeof(post), "%s", v + mark + 1);
    DrawText(pre, (int)vx, (int)y, 12, c);
    float dx = vx + (float)MeasureText(pre, 12);
    int dw = MeasureText(one, 12);
    DrawRectangle((int)dx - 1, (int)y - 1, dw + 2, 14, Color{ 255, 220, 60, 255 });
    DrawText(one, (int)dx, (int)y, 12, Color{ 20, 24, 28, 255 });
    DrawText(post, (int)(dx + dw), (int)y, 12, c);
}

static void DrawGenScreen(Rectangle lcd)
{
    Gen &g = app.gen;
    DrawRectangleRounded((Rectangle){ lcd.x - 5, lcd.y - 5, lcd.width + 10, lcd.height + 10 }, 0.03f, 6, LCD_BEZEL);
    DrawRectangleRec(lcd, LCD_BG);
    DrawText("RIGOL", (int)(lcd.x + 8), (int)(lcd.y + 4), 15, Color{ 0, 220, 160, 255 });
    DrawText("CH1", (int)(lcd.x + 78), (int)(lcd.y + 5), 13, g.sel == 0 ? Color{ 0, 230, 80, 255 } : Color{ 80, 110, 80, 255 });
    DrawText("CH2", (int)(lcd.x + 114), (int)(lcd.y + 5), 13, g.sel == 1 ? Color{ 80, 180, 255, 255 } : Color{ 70, 90, 110, 255 });
    if (g.counter) DrawText("COUNTER", (int)(lcd.x + 160), (int)(lcd.y + 6), 12, ORANGE);

    if (g.page == GP_HELP) {
        const char *help[] = {
            "DG1022  20 MHz  dual channel",
            "Sine 20 MHz   Square 5 MHz",
            "Ramp 150 kHz  Pulse 3 MHz",
            "CH1 0.004 V..20 Vpp into High Z",
            "CH2 0.004 V..6 Vpp into High Z",
            "50 ohm load doubles scope V",
            "Mod, Sweep, Burst are CH1",
            "Output ON sends the BNC signal",
            "to the scope channel.",
        };
        for (int i = 0; i < 9; i++)
            DrawText(help[i], (int)(lcd.x + 10), (int)(lcd.y + 28 + i * 18), 12, Color{ 210, 220, 210, 255 });
    } else if (g.view == 0) {
        for (int ch = 0; ch < 2; ch++) {
            GenCh &c = g.ch[ch];
            Color col = ch == 0 ? Color{ 0, 230, 80, 255 } : Color{ 90, 180, 255, 255 };
            float y0 = lcd.y + 26 + ch * 78;
            char vs[48];
            bool sel = g.sel == ch;
            bool showEntry = sel && g.entry;
            auto val = [&](Param p, char *dst, int *mark) {
                *mark = -1;
                if (showEntry && g.edit == p) { std::snprintf(dst, 48, "%s_", g.ebuf); return; }
                bool cur = sel && g.edit == p;
                if (p == PAR_FREQ) *mark = FmtFreqAt(dst, 48, c.freq, g.digit, cur);
                else if (p == PAR_PER) *mark = FmtTimeAt(dst, 48, 1.0 / c.freq, g.digit, cur);
                else if (p == PAR_HI) FmtVolt(dst, 48, c.offset + c.ampl * 0.5);
                else if (p == PAR_AMPL) FmtVpp(dst, 48, c.ampl);
                else if (p == PAR_LO) FmtVolt(dst, 48, c.offset - c.ampl * 0.5);
                else if (p == PAR_OFF) FmtVolt(dst, 48, c.offset);
                else std::snprintf(dst, 48, "%.3f deg", c.phase);
            };
            int mark = -1;
            if (c.wave != W_NOISE && !c.dc) {
                bool per = sel && g.editPeriod;
                val(per ? PAR_PER : PAR_FREQ, vs, &mark);
                DrawGenValue(lcd.x + 8, y0, per ? "Period" : "Freq", vs, col, sel && (g.edit == PAR_FREQ || g.edit == PAR_PER), mark);
            } else DrawGenValue(lcd.x + 8, y0, c.dc ? "DC" : "Noise", c.dc ? "offset" : "5 MHz", col, false, -1);
            val(g.amplIsHi && sel ? PAR_HI : PAR_AMPL, vs, &mark);
            DrawGenValue(lcd.x + 8, y0 + 16, g.amplIsHi && sel ? "High" : "Ampl", vs, col, sel && (g.edit == PAR_AMPL || g.edit == PAR_HI), -1);
            val(g.offIsLo && sel ? PAR_LO : PAR_OFF, vs, &mark);
            DrawGenValue(lcd.x + 8, y0 + 32, g.offIsLo && sel ? "Low" : "Offset", vs, WHITE, sel && (g.edit == PAR_OFF || g.edit == PAR_LO), -1);
            const char *row = "Phase";
            if (c.wave == W_SQUARE || c.wave == W_PULSE) { std::snprintf(vs, 48, "%.1f %%", c.duty); row = "Duty"; }
            else if (c.wave == W_RAMP) { std::snprintf(vs, 48, "%.1f %%", c.symmetry); row = "Symm"; }
            else val(PAR_PHASE, vs, &mark);
            DrawGenValue(lcd.x + 8, y0 + 48, row, vs, WHITE, sel && (g.edit == PAR_PHASE || g.edit == PAR_DUTY || g.edit == PAR_SYMM), -1);
            DrawShapeIn((Rectangle){ lcd.x + 196, y0, 52, 52 }, ch, col);
        }
    } else {
        int ch = g.view == 2 ? 1 : 0;
        Color col = ch == 0 ? Color{ 0, 230, 80, 255 } : Color{ 90, 180, 255, 255 };
        DrawShapeIn((Rectangle){ lcd.x + 12, lcd.y + 36, 200, 140 }, ch, col);
        char vs[48];
        FmtFreq(vs, 48, g.ch[ch].freq);
        DrawText(vs, (int)(lcd.x + 16), (int)(lcd.y + 182), 14, col);
        FmtVpp(vs, 48, g.ch[ch].ampl);
        DrawText(vs, (int)(lcd.x + 16), (int)(lcd.y + 200), 14, WHITE);
    }

    const char *mode = g.mode == 1 ? (g.modType == 0 ? "AM" : g.modType == 1 ? "FM" : g.modType == 2 ? "PM" : "FSK")
                     : g.mode == 2 ? "SWEEP" : g.mode == 3 ? "BURST" : "";
    if (mode[0]) DrawText(mode, (int)(lcd.x + 200), (int)(lcd.y + 6), 12, ORANGE);

    float mx = lcd.x + lcd.width - 62;
    for (int i = 0; i < 6; i++) {
        char a[32], b[32];
        bool hot = false;
        GenMenu(i, a, b, &hot);
        float y = lcd.y + 28 + i * 30.0f;
        Color col = hot ? Color{ 0, 230, 80, 255 } : WHITE;
        if (a[0]) DrawText(a, (int)mx, (int)y, 11, col);
        if (b[0]) DrawText(b, (int)mx, (int)(y + 12), 10, Color{ 180, 220, 180, 255 });
    }

    const char *w0 = g.ch[0].dc ? "DC" : g.ch[0].wave == W_SINE ? "Sine" : g.ch[0].wave == W_SQUARE ? "Square"
        : g.ch[0].wave == W_RAMP ? "Ramp" : g.ch[0].wave == W_PULSE ? "Pulse" : g.ch[0].wave == W_NOISE ? "Noise" : "Arb";
    const char *w1 = g.ch[1].dc ? "DC" : g.ch[1].wave == W_SINE ? "Sine" : g.ch[1].wave == W_SQUARE ? "Square"
        : g.ch[1].wave == W_RAMP ? "Ramp" : g.ch[1].wave == W_PULSE ? "Pulse" : g.ch[1].wave == W_NOISE ? "Noise" : "Arb";
    DrawText(w0, (int)(lcd.x + 8), (int)(lcd.y + lcd.height - 32), 11, WHITE);
    DrawText(w1, (int)(lcd.x + 8), (int)(lcd.y + lcd.height - 18), 11, WHITE);
    DrawText(g.ch[0].output ? "ON" : "OFF", (int)(lcd.x + 62), (int)(lcd.y + lcd.height - 34), 14,
             g.ch[0].output ? Color{ 0, 230, 80, 255 } : Color{ 140, 140, 140, 255 });
    DrawText(g.ch[1].output ? "ON" : "OFF", (int)(lcd.x + 62), (int)(lcd.y + lcd.height - 18), 14,
             g.ch[1].output ? Color{ 80, 180, 255, 255 } : Color{ 140, 140, 140, 255 });
    DrawText(g.highZ ? "HighZ" : "50ohm", (int)(lcd.x + 110), (int)(lcd.y + lcd.height - 32), 11, WHITE);
    DrawText(g.highZ ? "HighZ" : "50ohm", (int)(lcd.x + 110), (int)(lcd.y + lcd.height - 18), 11, WHITE);
    if (app.gmsgT > 0) DrawText(app.gmsg, (int)(lcd.x + 160), (int)(lcd.y + lcd.height - 18), 10, Color{ 255, 220, 120, 255 });
    if (g.counter) DrawText("Count: no signal", (int)(lcd.x + 160), (int)(lcd.y + lcd.height - 32), 10, ORANGE);
}
// -----------------------------------------------------------------------------
// RIGOL DS1062C
// -----------------------------------------------------------------------------
static void DrawOscilloscope(float ox, float oy, float ow, float oh)
{
    DrawRectangleRounded((Rectangle){ ox + 5, oy + 8, ow, oh }, 0.025f, 8, Fade(BLACK, 0.28f));
    DrawRectangleRounded((Rectangle){ ox, oy, ow, oh }, 0.025f, 8, SCOPE_BODY);
    DrawRectangleRoundedLinesEx((Rectangle){ ox, oy, ow, oh }, 0.025f, 8, 2.0f, SCOPE_BODY_DK);

    // Brand lives only above the screen, like the real bezel
    DrawRectangleRec((Rectangle){ ox + 14, oy + 12, 7, 22 }, Color{ 200, 160, 40, 255 });
    DrawText("RIGOL", (int)(ox + 26), (int)(oy + 10), 18, Color{ 30, 30, 32, 255 });
    DrawText("DS1062C", (int)(ox + 108), (int)(oy + 10), 16, Color{ 50, 50, 52, 255 });
    DrawText("DIGITAL OSCILLOSCOPE", (int)(ox + 108), (int)(oy + 26), 9, Color{ 90, 90, 92, 255 });
    DrawText("UltraZoom", (int)(ox + 268), (int)(oy + 12), 14, Color{ 60, 160, 80, 255 });
    DrawText("2 Channel  60MHz  400MSa/s", (int)(ox + 268), (int)(oy + 28), 9, Color{ 90, 90, 92, 255 });

    // MENU ON/OFF sits on the bezel above the five softkeys
    DrawText("MENU", (int)(ox + 398), (int)(oy + 8), 9, SCOPE_TEAL);
    DrawText("ON/OFF", (int)(ox + 392), (int)(oy + 18), 8, SCOPE_TEAL);
    DrawRoundBtn((Vector2){ ox + 414, oy + 38 }, 7, "", SCOPE_BODY_DK, 8);
    AddC(S_MENU_TOG, (Vector2){ ox + 414, oy + 38 }, 10);

    Rectangle lcd = { ox + 16, oy + 52, 358, 266 };
    DrawScopeScreen(lcd);

    float skx = ox + 396;
    float sky = oy + 78;
    float skh = 38;
    float skg = 12;
    for (int i = 0; i < 5; i++)
        Btn(S_SOFT + i, (Rectangle){ skx, sky + i * (skh + skg), 36, skh }, "", SCOPE_SOFTKEY, LABEL_DK, 10);

    // Control cluster starts at the top of the chassis (right of the screen)
    float cx = ox + 444;
    float cy = oy + 8;

    DrawKnob((Vector2){ cx + 22, cy + 44 }, 17, gSpin[0], SCOPE_KNOB, false);
    DrawRing((Vector2){ cx + 22, cy + 44 }, 20, 22, 220, 500, 18, SCOPE_TEAL);
    AddC(S_MKNOB, (Vector2){ cx + 22, cy + 44 }, 22);

    DrawPanelBox((Rectangle){ cx + 46, cy, 200, 76 }, "MENU");
    const char *menuBtns[6] = { "Measure", "Acquire", "Storage", "Cursor", "Display", "Utility" };
    const int menuIds[6] = { S_MEASURE, S_ACQUIRE, S_STORAGE, S_CURSOR, S_DISPLAY, S_UTILITY };
    for (int i = 0; i < 6; i++)
    {
        int col = i % 3, row = i / 3;
        Btn(menuIds[i], (Rectangle){ cx + 52 + col * 64.0f, cy + 18 + row * 26.0f, 60, 22 },
            menuBtns[i], SCOPE_BODY_LT, LABEL_DK, 9);
    }

    DrawPanelBox((Rectangle){ cx + 252, cy, 124, 76 }, "RUN CONTROL");
    Btn(S_AUTO, (Rectangle){ cx + 260, cy + 20, 50, 46 }, "AUTO", SCOPE_BODY_LT, LABEL_DK, 11);
    Btn(S_RUN, (Rectangle){ cx + 314, cy + 20, 54, 46 }, app.sc.running ? "RUN" : "STOP",
        app.sc.running ? SCOPE_BODY_LT : Color{ 190, 140, 130, 255 }, LABEL_DK, 11);

    Rectangle vert = { cx, cy + 86, 124, 210 };
    DrawPanelBox(vert, "VERTICAL");
    Color c1 = app.sc.ch[0].on ? SCOPE_GREEN_BTN : Color{ 150, 150, 140, 255 };
    Color c2 = app.sc.ch[1].on ? SCOPE_GREEN_BTN : Color{ 150, 150, 140, 255 };
    Btn(S_CH1, (Rectangle){ vert.x + 8,  vert.y + 20, 48, 30 }, "CH1", c1, LABEL_DK, 13);
    Btn(S_CH2, (Rectangle){ vert.x + 8,  vert.y + 54, 48, 30 }, "CH2", c2, LABEL_DK, 13);
    Btn(S_MATH, (Rectangle){ vert.x + 8,  vert.y + 90, 48, 24 }, "MATH", app.sc.mathOn ? SCOPE_GREEN_BTN : SCOPE_BODY_LT, LABEL_DK, 10);
    Btn(S_REF, (Rectangle){ vert.x + 8,  vert.y + 118, 48, 24 }, "REF", app.sc.refOn ? SCOPE_GREEN_BTN : SCOPE_BODY_LT, LABEL_DK, 10);

    Vector2 vPos = { vert.x + 88, vert.y + 50 };
    DrawKnobWithCaption(vPos, 16, gSpin[1], SCOPE_KNOB, false, "POSITION", NULL);
    DrawArrowsUD(vPos, 16, S_VPOS);
    AddC(S_VPOS, vPos, 18);
    Btn(S_OFF, (Rectangle){ vert.x + 68, vert.y + 82, 40, 20 }, "OFF", SCOPE_BODY_LT, LABEL_DK, 10);
    Vector2 vScale = { vert.x + 88, vert.y + 152 };
    DrawKnobWithCaption(vScale, 22, gSpin[2], SCOPE_KNOB, true, NULL, "SCALE");
    DrawArrowsLR(vScale, 16, S_VSCALE);
    AddC(S_VSCALE, vScale, 24);

    Rectangle horz = { cx + 128, cy + 86, 112, 210 };
    DrawPanelBox(horz, "HORIZONTAL");
    Vector2 hPos = { horz.x + 56, horz.y + 50 };
    DrawKnobWithCaption(hPos, 16, gSpin[3], SCOPE_KNOB, false, "POSITION", NULL);
    DrawArrowsUD(hPos, 16, S_HPOS);
    AddC(S_HPOS, hPos, 18);
    Btn(S_HMENU, (Rectangle){ horz.x + 28, horz.y + 82, 56, 22 }, "MENU", SCOPE_BODY_LT, LABEL_DK, 11);
    Vector2 hScale = { horz.x + 56, horz.y + 152 };
    DrawKnobWithCaption(hScale, 22, gSpin[4], SCOPE_KNOB, true, NULL, "SCALE");
    DrawArrowsLR(hScale, 16, S_HSCALE);
    AddC(S_HSCALE, hScale, 24);

    Rectangle trig = { cx + 244, cy + 86, 132, 210 };
    DrawPanelBox(trig, "TRIGGER");
    Vector2 tLev = { trig.x + 66, trig.y + 50 };
    DrawKnobWithCaption(tLev, 16, gSpin[5], SCOPE_KNOB, false, "LEVEL", NULL);
    DrawArrowsUD(tLev, 16, S_TLEV);
    AddC(S_TLEV, tLev, 18);
    Btn(S_TMENU, (Rectangle){ trig.x + 20, trig.y + 84, 92, 22 }, "MENU",  SCOPE_BODY_LT, LABEL_DK, 11);
    Btn(S_T50, (Rectangle){ trig.x + 20, trig.y + 112, 92, 22 }, "50%",   SCOPE_BODY_LT, LABEL_DK, 11);
    Btn(S_TFORCE, (Rectangle){ trig.x + 20, trig.y + 140, 92, 22 }, "FORCE", SCOPE_BODY_LT, LABEL_DK, 11);

    DrawUSBPort((Rectangle){ ox + 26, oy + oh - 82, 44, 22 });

    float by = oy + oh - 72;
    DrawBNC((Vector2){ ox + 470, by }, 16, METAL);
    DrawText("CH 1", (int)(ox + 456), (int)(by + 24), 10, LABEL_DK);
    DrawBNC((Vector2){ ox + 548, by }, 16, METAL);
    DrawText("CH 2", (int)(ox + 534), (int)(by + 24), 10, LABEL_DK);
    DrawBNC((Vector2){ ox + 636, by }, 16, METAL);
    DrawText("EXT TRIG", (int)(ox + 610), (int)(by + 24), 10, LABEL_DK);
    DrawWarningTriangle((Vector2){ ox + 674, by - 18 }, 8);
    DrawText("CAT II", (int)(ox + 664), (int)(by - 8), 7, Color{ 90, 90, 90, 255 });
    DrawUSBPort((Rectangle){ ox + 718, by - 10, 28, 36 });

    DrawFoot(ox + 40, oy + oh - 14, 50, 16);
    DrawFoot(ox + ow - 90, oy + oh - 14, 50, 16);
}

// -----------------------------------------------------------------------------
// RIGOL DG1022
// -----------------------------------------------------------------------------
static void DrawWaveformKey(Rectangle r, const char *name, Color face, int kind, bool selected)
{
    Color f = selected ? GEN_SINE : face;
    DrawRaisedRect(r, f, 4);
    DrawRectangleRoundedLinesEx(r, 0.12f, 4, 1.2f, ColorBrightness(f, -0.4f));
    Rectangle icon = { r.x + 6, r.y + 4, r.width - 12, r.height * 0.45f };
    Color ic = selected ? WHITE : Color{ 40, 50, 70, 255 };
    switch (kind)
    {
        case 0: DrawMiniSine(icon, ic); break;
        case 1: DrawMiniSquare(icon, ic); break;
        case 2: DrawMiniRamp(icon, ic); break;
        case 3: DrawMiniPulse(icon, ic); break;
        case 4: DrawMiniNoise(icon, ic); break;
        default: DrawMiniArb(icon, ic); break;
    }
    Rectangle lab = { r.x, r.y + r.height * 0.52f, r.width, r.height * 0.42f };
    DrawCenteredText(name, lab, 10, selected ? WHITE : LABEL_DK);
}

static void DrawArrowKey(Rectangle r, int dir)
{
    DrawBevelButton(r, "", GEN_KEY, LABEL_DK, 10);
    Vector2 c = { r.x + r.width * 0.5f, r.y + r.height * 0.5f };
    if (dir < 0)
        DrawTriangle((Vector2){ c.x - 8, c.y }, (Vector2){ c.x + 6, c.y - 8 }, (Vector2){ c.x + 6, c.y + 8 }, LABEL_DK);
    else
        DrawTriangle((Vector2){ c.x + 8, c.y }, (Vector2){ c.x - 6, c.y - 8 }, (Vector2){ c.x - 6, c.y + 8 }, LABEL_DK);
}

static void DrawGenerator(float ox, float oy, float ow, float oh)
{
    DrawRectangleRounded((Rectangle){ ox - 18, oy + 28, 26, oh - 70 }, 0.35f, 6, GEN_BODY_DK);
    DrawRectangleRounded((Rectangle){ ox + ow - 8, oy + 28, 26, oh - 70 }, 0.35f, 6, GEN_BODY_DK);

    DrawRectangleRounded((Rectangle){ ox + 4, oy + 8, ow, oh }, 0.03f, 8, Fade(BLACK, 0.28f));
    DrawRectangleRounded((Rectangle){ ox, oy, ow, oh }, 0.03f, 8, GEN_BODY);
    DrawRectangleRoundedLinesEx((Rectangle){ ox, oy, ow, oh }, 0.03f, 8, 2.0f, GEN_BODY_DK);

    DrawRectangleRec((Rectangle){ ox + ow * 0.40f, oy + 6, 156, 18 }, Color{ 40, 42, 46, 255 });
    DrawText("RIGOL", (int)(ox + ow * 0.40f + 8), (int)(oy + 8), 12, Color{ 230, 200, 60, 255 });
    DrawText("DG1022", (int)(ox + ow * 0.40f + 72), (int)(oy + 8), 12, WHITE);

    Rectangle lcd = { ox + 16, oy + 30, 320, 236 };
    DrawGenScreen(lcd);

    float skx = ox + 344;
    float sky = oy + 38;
    float skh = 30;
    float skg = 8;
    for (int i = 0; i < 6; i++)
        Btn(G_SOFT + i, (Rectangle){ skx, sky + i * (skh + skg), 34, skh }, "", GEN_SOFTKEY, LABEL_DK, 10);

    DrawText("Waveform", (int)(ox + 392), (int)(oy + 26), 10, Color{ 90, 100, 110, 255 });
    const char *waves[] = { "Sine", "Square", "Ramp", "Pulse", "Noise", "Arb" };
    float wx = ox + 392, wy = oy + 40, ww = 60, wh = 48, wg = 4;
    for (int i = 0; i < 6; i++) {
        Rectangle wr{ wx + i * (ww + wg), wy, ww, wh };
        DrawWaveformKey(wr, waves[i], GEN_KEY, i, i == (int)app.gen.ch[app.gen.sel].wave);
        AddR(G_WAVE + i, wr);
    }

    const char *fn[6] = { "Mod", "Utility", "Sweep", "Store", "Burst", "Help" };
    const int fnId[6] = { G_MOD, G_UTIL, G_SWEEP, G_STORE, G_BURST, G_HELP };
    float fx = ox + 392, fy = oy + 98, fw = 72, fh = 30, fgx = 8, fgy = 8;
    for (int i = 0; i < 6; i++)
    {
        int col = i % 2, row = i / 2;
        Btn(fnId[i], (Rectangle){ fx + col * (fw + fgx), fy + row * (fh + fgy), fw, fh },
            fn[i], GEN_KEY, LABEL_DK, 12);
    }
    Btn(G_LOCAL, (Rectangle){ fx, fy + 3 * (fh + fgy) + 6, fw, 22 }, "Local", GEN_KEY, LABEL_DK, 11);

    const char *keys[12] = { "1", "2", "3", "+/-", "4", "5", "6", "0", "7", "8", "9", "." };
    float kx = ox + 560, ky = oy + 98, kw = 40, kh = 32, kg = 6;
    for (int i = 0; i < 12; i++)
    {
        int col = i % 4, row = i / 4;
        int fs = (i == 11) ? 22 : 14;
        Btn(G_KEY + i, (Rectangle){ kx + col * (kw + kg), ky + row * (kh + kg), kw, kh },
            keys[i], GEN_KEY, LABEL_DK, fs);
    }

    Vector2 knob = { ox + 860, oy + 72 };
    DrawKnob(knob, 34, gSpin[6], GEN_KEY, true);
    DrawCircleLines((int)knob.x, (int)knob.y, 38, GEN_BODY_DK);
    AddC(G_KNOB, knob, 38);

    Rectangle leftA{ ox + 810, oy + 122, 40, 30 };
    Rectangle rightA{ ox + 870, oy + 122, 40, 30 };
    DrawArrowKey(leftA, -1);
    DrawArrowKey(rightA, +1);
    AddR(G_LEFT, leftA);
    AddR(G_RIGHT, rightA);

    Rectangle view = { ox + 344, oy + 278, 36, 28 };
    Btn(G_VIEW, view, "", GEN_KEY, LABEL_DK, 10);
    DrawTriangle((Vector2){ view.x + 18, view.y + 7 },
                 (Vector2){ view.x + 8,  view.y + 20 },
                 (Vector2){ view.x + 28, view.y + 20 }, LABEL_DK);

    DrawUSBPort((Rectangle){ ox + 24, oy + 278, 44, 22 });

    Btn(G_OUT1, (Rectangle){ ox + 410, oy + 278, 74, 26 }, "Output1",
        app.gen.ch[0].output ? GEN_YELLOW : Color{ 214, 214, 218, 255 }, LABEL_DK, 12);

    Rectangle split = { ox + 492, oy + 278, 62, 26 };
    DrawRaisedRect(split, GEN_KEY, 3);
    DrawRectangleRec((Rectangle){ split.x + 1, split.y + 1, split.width * 0.5f - 1, split.height - 2 },
                     Fade(GEN_YELLOW, 0.8f));
    DrawRectangleRec((Rectangle){ split.x + split.width * 0.5f, split.y + 1, split.width * 0.5f - 1, split.height - 2 },
                     Fade(GEN_BLUE, 0.8f));
    DrawCenteredText("CH1 CH2", split, 9, LABEL_DK);
    AddR(G_CHSEL, split);

    Btn(G_OUT2, (Rectangle){ ox + 562, oy + 278, 74, 26 }, "Output2",
        app.gen.ch[1].output ? GEN_BLUE : Color{ 180, 190, 200, 255 },
        app.gen.ch[1].output ? WHITE : LABEL_DK, 12);
    DrawText("Counter", (int)(ox + 662), (int)(oy + 284), 11, Color{ 90, 100, 110, 255 });

    DrawBNC((Vector2){ ox + 446, oy + 342 }, 18, GEN_YELLOW);
    DrawText("CH1", (int)(ox + 433), (int)(oy + 364), 10, LABEL_DK);
    DrawBNC((Vector2){ ox + 598, oy + 342 }, 18, GEN_BLUE);
    DrawText("CH2", (int)(ox + 585), (int)(oy + 364), 10, LABEL_DK);
    DrawBNC((Vector2){ ox + 710, oy + 342 }, 18, METAL);
    DrawWarningTriangle((Vector2){ ox + 744, oy + 322 }, 8);

    DrawText("Function/Arbitrary Waveform Generator",
             (int)(ox + ow - 290), (int)(oy + oh - 22), 10, Color{ 90, 100, 110, 255 });
}

// Lab sheet beside the scope.
// Umax = Up = Upp/2.  Ū is the generator offset, shown as set.
static bool gLabOn = true;

static void DrawLabEye(float x, float y, float w)
{
    Rectangle r{ x + w - 38, y + 8, 28, 22 };
    DrawRectangleRounded(r, 0.4f, 6, Color{ 255, 255, 255, 255 });
    DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.2f, Color{ 70, 74, 82, 255 });
    int cx = (int)(r.x + r.width * 0.5f);
    int cy = (int)(r.y + r.height * 0.5f);
    Color ink{ 36, 40, 48, 255 };
    DrawEllipseLines(cx, cy, 9.0f, 5.5f, ink);
    DrawCircleV(Vector2{ (float)cx, (float)cy }, 2.3f, ink);
    if (!gLabOn) DrawLineEx(Vector2{ (float)cx - 8, (float)cy + 5 }, Vector2{ (float)cx + 8, (float)cy - 5 }, 1.7f, ink);
    AddR(ID_LAB, r);
}

static void DrawLabPanel(float x, float y, float w, float h)
{
    if (!gLabOn) {
        DrawLabEye(x, y, w);
        return;
    }
    int src = app.sc.active == 1 ? 1 : 0;
    const ScopeCh &ch = app.sc.ch[src];
    double cy = ch.vdiv;
    double cx = app.sc.delayOn ? app.sc.delayDiv : app.sc.tdiv;
    bool live = app.gen.ch[src].output && ch.coupling != 2 && cy > 0 && cx > 0;

    double ly = 0, lx = 0, ldc = 0, upp = 0, udc = 0, uac = 0, u = 0, per = 0;
    bool hasPer = false;
    if (live) {
        const int N = 480;
        double buf[480];
        double f = std::max(app.gen.ch[src].freq, 1.0);
        double span = 3.0 / f;
        double dt = span / (N - 1);
        double sum = 0, sum2 = 0, vmin = 1e300, vmax = -1e300;
        for (int i = 0; i < N; i++) {
            double v = ScopeVolts(src, i * dt);
            buf[i] = v;
            sum += v;
            sum2 += v * v;
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        double mean = sum / N;
        udc = mean;
        ldc = udc / cy;
        upp = vmax - vmin;
        ly = upp / cy;
        double var = sum2 / N - mean * mean;
        if (var < 0) var = 0;
        uac = std::sqrt(var);
        u = std::sqrt(udc * udc + uac * uac);
        double mid = 0.5 * (vmin + vmax);
        int rises = 0;
        double first = 0, last = 0;
        for (int i = 1; i < N; i++) {
            if (buf[i - 1] < mid && buf[i] >= mid) {
                double frac = (mid - buf[i - 1]) / std::max(1e-15, buf[i] - buf[i - 1]);
                double tc = (i - 1 + frac) * dt;
                if (rises == 0) first = tc;
                last = tc;
                rises++;
            }
        }
        if (rises >= 2 && last > first) {
            per = (last - first) / (rises - 1);
            lx = per / cx;
            hasPer = per > 0;
        }
    }

    DrawRectangleRounded(Rectangle{ x, y, w, h }, 0.03f, 6, Color{ 248, 246, 240, 255 });
    DrawRectangleRoundedLinesEx(Rectangle{ x, y, w, h }, 0.03f, 6, 1.5f, Color{ 90, 86, 74, 255 });
    float tx = x + 12;
    float ty = y + 10;
    DrawPl(src ? "Kanał 2" : "Kanał 1", tx, ty, 20, Color{ 30, 30, 32, 255 });
    ty += 26;
    if (!app.gen.ch[src].output) DrawPl("Brak sygnału — wyjście wyłączone", tx, ty, 14, Color{ 140, 50, 40, 255 });
    else if (ch.coupling == 2) DrawPl("Sprzężenie GND", tx, ty, 14, Color{ 140, 50, 40, 255 });
    else DrawPl("Odczyt z ekranu oscyloskopu", tx, ty, 14, Color{ 80, 80, 84, 255 });
    ty += 24;

    char a[64], b[64], line[96];
    auto show = [&](const char *name, const char *formula, bool known, bool needSignal = true) {
        DrawPl(name, tx, ty, 15, Color{ 24, 24, 28, 255 });
        ty += 18;
        if (!known) DrawPl(formula, tx, ty, 15, Color{ 150, 60, 40, 255 });
        else if (needSignal && !live) DrawPl("—", tx, ty, 15, Color{ 90, 90, 96, 255 });
        else DrawPl(formula, tx, ty, 15, Color{ 20, 70, 40, 255 });
        ty += 22;
    };

    FmtVolt(a, 64, cy);
    std::snprintf(line, sizeof(line), "Cy = %s/dz", a);
    DrawPl(line, tx, ty, 14, Color{ 50, 50, 56, 255 });
    ty += 18;
    FmtTime(b, 64, cx);
    std::snprintf(line, sizeof(line), "Cx = %s/dz", b);
    DrawPl(line, tx, ty, 14, Color{ 50, 50, 56, 255 });
    ty += 26;

    if (live) {
        char lyS[32], cyS[32], uppS[32];
        std::snprintf(lyS, sizeof(lyS), "%.3f", ly);
        FmtVolt(cyS, 32, cy);
        FmtVolt(uppS, 32, upp);
        std::snprintf(line, sizeof(line), "Upp = %s dz · %s = %s", lyS, cyS, uppS);
    }
    show("Wartość międzyszczytowa", live ? line : "", true);

    if (live) {
        char upS[32];
        FmtVolt(upS, 32, upp * 0.5);
        std::snprintf(line, sizeof(line), "Umax = Up = Upp/2 = %s", upS);
    }
    show("Wartość szczytowa", live ? line : "", true);

    {
        char usrS[32];
        FmtVolt(usrS, 32, app.gen.ch[src].offset);
        std::snprintf(line, sizeof(line), "Ū = %s", usrS);
    }
    show("Wartość średnia", line, true, false);

    if (live) {
        char uacS[32];
        FmtVolt(uacS, 32, uac);
        std::snprintf(line, sizeof(line), "Uac = %s", uacS);
    }
    show("Wartość skuteczna składowej zmiennej", live ? line : "", true);

    if (live) {
        char uS[32], udcS[32], uacS[32];
        FmtVolt(udcS, 32, udc);
        FmtVolt(uacS, 32, uac);
        FmtVolt(uS, 32, u);
        std::snprintf(line, sizeof(line), "U = √(%s² + %s²) = %s", udcS, uacS, uS);
    }
    show("Wartość skuteczna (ze składową stałą)", live ? line : "", true);

    if (live) {
        char ldcS[32], cyS[32], udcS[32];
        std::snprintf(ldcS, sizeof(ldcS), "%.3f", ldc);
        FmtVolt(cyS, 32, cy);
        FmtVolt(udcS, 32, udc);
        std::snprintf(line, sizeof(line), "Udc = %s dz · %s = %s", ldcS, cyS, udcS);
    }
    show("Składowa stała", live ? line : "", true);

    if (live && hasPer) {
        char lxS[32], cxS[32], tS[32];
        std::snprintf(lxS, sizeof(lxS), "%.3f", lx);
        FmtTime(cxS, 32, cx);
        FmtTime(tS, 32, per);
        std::snprintf(line, sizeof(line), "T = %s dz · %s = %s", lxS, cxS, tS);
    }
    show("Okres sygnału", (live && hasPer) ? line : "T = lx·Cx = —", true);

    if (live && hasPer) {
        char fS[32];
        FmtFreq(fS, 32, 1.0 / per);
        std::snprintf(line, sizeof(line), "f = 1/T = %s", fS);
    }
    show("Częstotliwość sygnału", (live && hasPer) ? line : "f = 1/T = —", true);
    (void)a; (void)b;
    DrawLabEye(x, y, w);
}

// -----------------------------------------------------------------------------
int main()
{
    int failed = AppTest("selftest.txt");
    AppInit();
    const int W = 1880;
    const int H = 780;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(W, H, "RIGOL DS1062C  +  RIGOL DG1022");
    SetTargetFPS(60);
    {
        int cps[128];
        int n = 0;
        for (int c = 32; c < 127; c++) cps[n++] = c;
        const int extra[] = {
            0x00B1, 0x00B2, 0x00B7, 0x00D7, 0x03C0, 0x221A, 0x2014,
            0x00D3, 0x00F3, 0x0104, 0x0105, 0x0106, 0x0107,
            0x0118, 0x0119, 0x0141, 0x0142, 0x0143, 0x0144,
            0x015A, 0x015B, 0x016A, 0x016B, 0x0179, 0x017A, 0x017B, 0x017C
        };
        for (int c : extra) cps[n++] = c;
        const char *path = "C:/Windows/Fonts/segoeui.ttf";
        if (!FileExists(path)) path = "C:/Windows/Fonts/arial.ttf";
        if (FileExists(path)) {
            gUi = LoadFontEx(path, 32, cps, n);
            SetTextureFilter(gUi.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
    InitAudioDevice();
    Wave wave{};
    wave.frameCount = 1600;
    wave.sampleRate = 44100;
    wave.sampleSize = 16;
    wave.channels = 1;
    short *pcm = (short *)MemAlloc(1600 * sizeof(short));
    for (int i = 0; i < 1600; i++) {
        float e = 1.0f - (float)i / 1600.0f;
        pcm[i] = (short)(std::sin(2.0 * PI * 1400.0 * i / 44100.0) * 7000.0 * e);
    }
    wave.data = pcm;
    Sound click = LoadSoundFromWave(wave);
    UnloadWave(wave);
    app.audioReady = IsAudioDeviceReady();

    int capture = 0;
    int nudgeId = 0, nudgeDir = 0;
    int dragAxis = 0;
    Vector2 capAt{};
    float acc = 0, pendingX = 0, pendingY = 0;
    bool dragged = false;
    (void)failed;

    while (!WindowShouldClose())
    {
        Vector2 screen = GetMousePosition();
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && ctrl) {
            Vector2 before = GetScreenToWorld2D(screen, cam);
            cam.zoom = Clamp(cam.zoom * std::pow(1.12f, wheel), 0.35f, 8.0f);
            Vector2 after = GetScreenToWorld2D(screen, cam);
            cam.target.x += before.x - after.x;
            cam.target.y += before.y - after.y;
        }
        if (ctrl && (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0))) {
            cam.zoom = 1.0f;
            cam.target = Vector2{ 0, 0 };
        }
        Vector2 m = GetScreenToWorld2D(screen, cam);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            nudgeId = 0;
            if (ctrl) {
                capture = 0;
                gHeld = 0;
                dragged = true;
            } else {
                int ni = PickNudge(m);
                if (ni >= 0) { nudgeId = gNudges[ni].id; nudgeDir = gNudges[ni].dir; capture = 0; }
                else capture = PickHit(m);
                dragged = false;
                gHeld = capture;
            }
            capAt = m;
            acc = 0;
            pendingX = pendingY = 0;
            dragAxis = 0;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && IsKnob(capture)) {
            Vector2 d = GetMouseDelta();
            // Lock the first clear direction for the whole drag. Re-picking the
            // bigger axis each frame reverses a turn halfway around the knob.
            if (dragAxis == 0) {
                pendingX += d.x;
                pendingY += -d.y;
                if (std::fabs(pendingX) >= 4.0f || std::fabs(pendingY) >= 4.0f)
                    dragAxis = std::fabs(pendingX) > std::fabs(pendingY) ? 1 : 2;
                if (dragAxis == 1) acc = pendingX;
                else if (dragAxis == 2) acc = pendingY;
            } else acc += dragAxis == 1 ? d.x : -d.y;
            int steps = (int)std::trunc(acc / 6.0f);
            if (steps) {
                acc -= steps * 6.0f;
                TurnKnob(capture, steps);
                dragged = true;
            }
        }
        if (wheel != 0.0f && !ctrl) {
            int id = PickHit(m);
            if (IsKnob(id)) TurnKnob(id, wheel > 0 ? 1 : -1);
        }
        bool viewMoved = std::fabs(cam.zoom - 1.0f) > 0.02f;
        bool ctrlPan = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ctrl && capture == 0 && !nudgeId;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || ctrlPan ||
            (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && capture == 0 && !nudgeId && viewMoved)) {
            Vector2 d = GetMouseDelta();
            cam.target.x -= d.x / cam.zoom;
            cam.target.y -= d.y / cam.zoom;
        }
        int ch = GetCharPressed();
        while (ch > 0) { AppKey(ch); ch = GetCharPressed(); }
        if (IsKeyPressed(KEY_BACKSPACE)) AppKey(259);
        if (IsKeyPressed(KEY_ESCAPE)) AppKey(256);
        if (IsKeyPressed(KEY_LEFT)) AppTurn(G_LEFT, 0);
        if (IsKeyPressed(KEY_RIGHT)) AppTurn(G_RIGHT, 0);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && nudgeId && !capture) {
            if (Vector2Distance(m, capAt) * cam.zoom < 8.0f) TurnKnob(nudgeId, nudgeDir);
            nudgeId = 0;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && capture) {
            if (Vector2Distance(m, capAt) * cam.zoom < 8.0f && !dragged) {
                float local = 0;
                for (int i = 0; i < gHitN; i++) if (gHits[i].id == capture) local = m.x - gHits[i].r.x;
                if (capture == ID_LAB) gLabOn = !gLabOn;
                else if (IsKnob(capture)) AppKnobClick(capture);
                else AppPressCh(capture, local);
                bool beep = capture == ID_LAB ? app.sc.beep : (capture >= G_WAVE ? app.gen.beep : app.sc.beep);
                if (app.audioReady && beep) PlaySound(click);
            }
            capture = 0;
            gHeld = 0;
        }

        AppTick(GetTime());

        BeginDrawing();
        ClearBackground(Color{ 236, 236, 238, 255 });
        ClearHits();
        BeginMode2D(cam);
        DrawText("DG1022 CH1 -> DS1062C CH1    DG1022 CH2 -> DS1062C CH2    scope input 1 Mohm",
                 356, 12, 16, Color{ 55, 60, 66, 255 });
        DrawLabPanel(8, 36, 340, 700);
        DrawOscilloscope(356, 40, 830, 540);
        DrawGenerator(1206, 100, 950, 450);
        DrawText("Ctrl+drag moves the view. Ctrl+scroll zooms, Ctrl+0 resets. Wheel on a knob still turns that knob.",
                 356, 742, 15, Color{ 70, 74, 80, 255 });
        EndMode2D();
        EndDrawing();
    }

    if (gUi.texture.id) UnloadFont(gUi);
    UnloadSound(click);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
