/*
===========================================================================
Name        : WeatherIcons.cpp
Author      : Brandon Van Pelt
Description : Drawn weather icons (see WeatherIcons.h).

              Every shape is expressed as a fraction of the requested size, so
              the same code draws the 92 px Home icon and the 52 px forecast
              icons. Each shape is painted twice — once inflated in the outline
              colour, then at size in its fill colour — which outlines the union
              of overlapping circles cleanly without leaving interior arcs.
===========================================================================
*/
#include "WeatherIcons.h"

#include <Arduino_GFX_Library.h>
#include <math.h>
#include <string.h>

static Arduino_GFX* g = nullptr;

// --- Palette (RGB565), taken off the reference sheet ------------------------
static const uint16_t COL_SUN    = 0xF609;   // warm yellow
static const uint16_t COL_MOON   = 0xF6D1;   // pale gold
static const uint16_t COL_CLOUD  = 0xD77F;   // light blue-white cloud
static const uint16_t COL_GRAY   = 0x9D35;   // overcast grey
static const uint16_t COL_GRAY_D = 0x6BD0;   // storm grey
static const uint16_t COL_RAIN   = 0x5D5C;   // rain blue
static const uint16_t COL_SNOW   = 0xEFBF;   // blue-white: pure white vanishes on the light theme
static const uint16_t COL_BOLT   = 0xF649;

// Outline thickness for the icon being drawn. A 2 px outline that frames the
// 92 px Home icon swallows the detail of a 52 px forecast icon, so it tracks
// the requested size. Set once at the top of wicon_draw().
static int OUTLINE_W = 2;

static inline int R(float v) { return (int)lroundf(v); }

void wicon_begin(Arduino_GFX* gfx) { g = gfx; }

WeatherIcon wicon_fromOwm(const char* code)
{
    if (!code || strlen(code) < 2)
        return WICON_NONE;

    // "NNx" — NN is the condition group, x is 'd' or 'n'.
    bool night = (code[2] == 'n');

    if (!strncmp(code, "01", 2)) return night ? WICON_CLEAR_NIGHT : WICON_CLEAR_DAY;
    if (!strncmp(code, "02", 2)) return night ? WICON_FEW_NIGHT   : WICON_FEW_DAY;
    if (!strncmp(code, "03", 2)) return WICON_CLOUDS;
    if (!strncmp(code, "04", 2)) return WICON_OVERCAST;
    if (!strncmp(code, "09", 2)) return WICON_SHOWERS;
    if (!strncmp(code, "10", 2)) return WICON_RAIN;
    if (!strncmp(code, "11", 2)) return WICON_STORM;
    if (!strncmp(code, "13", 2)) return WICON_SNOW;
    if (!strncmp(code, "50", 2)) return WICON_MIST;
    return WICON_NONE;
}

// Outline shade that reads against the card it sits on: dark ink on a light
// theme, light ink on a dark one.
static uint16_t outlineFor(uint16_t bg)
{
    int r8 = ((bg >> 11) & 0x1F) << 3;
    int g8 = ((bg >> 5) & 0x3F) << 2;
    int b8 = (bg & 0x1F) << 3;
    int lum = (r8 * 77 + g8 * 150 + b8 * 29) >> 8;
    return (lum > 140) ? 0x29A8 /* near-black */ : 0xD73D /* near-white */;
}

// --- Primitives ------------------------------------------------------------

// A line `t` px thick, offset across its minor axis.
static void thickLine(int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    if (t < 1) t = 1;
    int half = t / 2;
    bool horizontal = (abs(x1 - x0) > abs(y1 - y0));
    for (int i = -half; i <= half; i++)
    {
        if (horizontal) g->drawLine(x0, y0 + i, x1, y1 + i, c);
        else            g->drawLine(x0 + i, y0, x1 + i, y1, c);
    }
}

// Cloud filling a box of (w x 0.68w) with its top-left at (x, y). `inflate`
// grows it evenly, which is how the outline pass works.
static void cloudShape(int x, int y, float w, uint16_t c, int inflate)
{
    int bottom = y + R(0.68f * w);
    g->fillCircle(x + R(0.22f * w), bottom - R(0.22f * w), R(0.22f * w) + inflate, c);
    g->fillCircle(x + R(0.75f * w), bottom - R(0.25f * w), R(0.25f * w) + inflate, c);
    g->fillCircle(x + R(0.46f * w), bottom - R(0.38f * w), R(0.30f * w) + inflate, c);

    int baseH = R(0.24f * w) + inflate;
    g->fillRect(x - inflate, bottom - baseH, R(w) + 2 * inflate, baseH + inflate, c);
}

static void cloud(int x, int y, float w, uint16_t fill, uint16_t outline)
{
    cloudShape(x, y, w, outline, OUTLINE_W);
    cloudShape(x, y, w, fill, 0);
}

// Crescent = disc at (cx, cy) r, minus a disc offset by (ox, oy) of radius cr.
// Drawn span by span rather than by painting the cut-out in the background
// colour: the cutting disc reaches well outside the icon box, and painting it
// would wipe whatever sits beside the icon.
static void crescentShape(int cx, int cy, int r, int ox, int oy, int cr, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++)
    {
        int half = (int)sqrtf((float)(r * r - dy * dy));
        int xs = cx - half, xe = cx + half;

        int ddy = dy - oy;
        if (cr > 0 && abs(ddy) < cr)
        {
            int chalf = (int)sqrtf((float)(cr * cr - ddy * ddy));
            int cxs = cx + ox - chalf, cxe = cx + ox + chalf;

            if (cxs <= xs && cxe >= xe)
                continue;                                   // this row is fully cut
            if (cxs > xs && cxe < xe)
            {                                               // cut splits the row
                g->drawFastHLine(xs, cy + dy, cxs - xs, c);
                g->drawFastHLine(cxe + 1, cy + dy, xe - cxe, c);
                continue;
            }
            if (cxe >= xs && cxs <= xs) xs = cxe + 1;
            else if (cxs > xs && cxs <= xe) xe = cxs - 1;
        }

        if (xe >= xs)
            g->drawFastHLine(xs, cy + dy, xe - xs + 1, c);
    }
}

// Inflating a crescent means growing the disc and shrinking the cut-out.
static void moon(int cx, int cy, float s, uint16_t outline)
{
    int r  = R(0.40f * s);
    int cr = R(0.34f * s);
    int ox = R(0.30f * s), oy = -R(0.26f * s);

    crescentShape(cx, cy, r + OUTLINE_W, ox, oy, cr - OUTLINE_W, outline);
    crescentShape(cx, cy, r, ox, oy, cr, COL_MOON);
}

static void sun(int cx, int cy, float s, uint16_t outline)
{
    int   disc = R(0.24f * s);
    float ri = 0.36f * s, ro = 0.48f * s;
    int   t = R(0.055f * s);
    if (t < 2) t = 2;

    for (int pass = 0; pass < 2; pass++)
    {
        uint16_t c = pass ? COL_SUN : outline;
        int      w = pass ? t : t + 2 * OUTLINE_W;
        for (int i = 0; i < 8; i++)
        {
            float a = i * (float)M_PI / 4.0f;
            thickLine(cx + R(cosf(a) * ri), cy + R(sinf(a) * ri),
                      cx + R(cosf(a) * ro), cy + R(sinf(a) * ro), w, c);
        }
    }

    // Disc last, so it covers the inner ends of the rays.
    g->fillCircle(cx, cy, disc + OUTLINE_W, outline);
    g->fillCircle(cx, cy, disc, COL_SUN);
}

// Slanted rain streaks under a cloud.
static void rainStreaks(int x0, int y0, float s, int count, uint16_t outline)
{
    int t  = R(0.05f * s); if (t < 2) t = 2;
    int dx = R(0.05f * s), dy = R(0.16f * s);
    for (int i = 0; i < count; i++)
    {
        int x = x0 + R((0.16f + i * 0.24f) * s);
        thickLine(x + dx, y0, x - dx, y0 + dy, t + OUTLINE_W, outline);
        thickLine(x + dx, y0, x - dx, y0 + dy, t, COL_RAIN);
    }
}

// Round drops, for the shower icon.
static void rainDrops(int x0, int y0, float s, uint16_t outline)
{
    int r = R(0.05f * s); if (r < 2) r = 2;
    for (int i = 0; i < 4; i++)
    {
        int x = x0 + R((0.14f + i * 0.22f) * s);
        int y = y0 + ((i & 1) ? R(0.12f * s) : 0);
        g->fillCircle(x, y, r + 1, outline);
        g->fillCircle(x, y, r, COL_RAIN);
    }
}

static void snowFlakes(int x0, int y0, float s, uint16_t outline)
{
    int arm  = R(0.085f * s); if (arm < 4) arm = 4;
    int core = (s >= 72.0f) ? 2 : 1;
    for (int i = 0; i < 3; i++)
    {
        int cx = x0 + R((0.20f + i * 0.28f) * s);
        int cy = y0 + ((i == 1) ? R(0.12f * s) : R(0.04f * s));
        for (int pass = 0; pass < 2; pass++)
        {
            uint16_t c = pass ? COL_SNOW : outline;
            int      t = pass ? core : core + 2 * OUTLINE_W;
            for (int k = 0; k < 3; k++)
            {
                float a = k * (float)M_PI / 3.0f;
                thickLine(cx - R(cosf(a) * arm), cy - R(sinf(a) * arm),
                          cx + R(cosf(a) * arm), cy + R(sinf(a) * arm), t, c);
            }
        }
    }
}

static void bolt(int cx, int y0, float s, uint16_t outline)
{
    int w = R(0.11f * s), h = R(0.30f * s);
    for (int pass = 0; pass < 2; pass++)
    {
        uint16_t c = pass ? COL_BOLT : outline;
        int      e = pass ? 0 : OUTLINE_W;
        g->fillTriangle(cx - w - e, y0 + h / 2 + e, cx + w + e, y0 - e, cx - e / 2, y0 + h / 2 + e, c);
        g->fillTriangle(cx + w + e, y0 + h / 2 - e, cx - w - e, y0 + h + e, cx + e / 2, y0 + h / 2 - e, c);
    }
}

// --- Entry points ----------------------------------------------------------
void wicon_clear(int cx, int cy, int size, uint16_t bg)
{
    if (!g || size < 1)
        return;
    g->fillRect(cx - size / 2, cy - size / 2, size, size, bg);
}

void wicon_draw(int cx, int cy, int size, WeatherIcon icon, uint16_t bg)
{
    if (!g || size < 8)
        return;

    const float s  = (float)size;
    const int   x0 = cx - size / 2;
    const int   y0 = cy - size / 2;
    const uint16_t ink = outlineFor(bg);

    OUTLINE_W = (size >= 72) ? 2 : 1;

    // Clear the box so an icon can replace another in place.
    g->fillRect(x0, y0, size, size, bg);

    switch (icon)
    {
        case WICON_CLEAR_DAY:
            sun(cx, cy, s, ink);
            break;

        case WICON_CLEAR_NIGHT:
            moon(cx, cy, s, ink);
            break;

        case WICON_FEW_DAY:
        case WICON_FEW_NIGHT:
            if (icon == WICON_FEW_DAY) sun(x0 + R(0.34f * s), y0 + R(0.32f * s), 0.62f * s, ink);
            else                       moon(x0 + R(0.36f * s), y0 + R(0.30f * s), 0.58f * s, ink);
            cloud(x0 + R(0.14f * s), y0 + R(0.40f * s), 0.80f * s, COL_CLOUD, ink);
            break;

        case WICON_CLOUDS:
            cloud(x0 + R(0.05f * s), y0 + R(0.20f * s), 0.90f * s, COL_CLOUD, ink);
            break;

        case WICON_OVERCAST:
            cloud(x0 + R(0.26f * s), y0 + R(0.08f * s), 0.68f * s, COL_GRAY_D, ink);
            cloud(x0 + R(0.02f * s), y0 + R(0.30f * s), 0.78f * s, COL_GRAY, ink);
            break;

        case WICON_SHOWERS:
            cloud(x0 + R(0.09f * s), y0 + R(0.06f * s), 0.82f * s, COL_GRAY, ink);
            rainDrops(x0, y0 + R(0.76f * s), s, ink);
            break;

        case WICON_RAIN:
            cloud(x0 + R(0.09f * s), y0 + R(0.06f * s), 0.82f * s, COL_CLOUD, ink);
            rainStreaks(x0, y0 + R(0.70f * s), s, 3, ink);
            break;

        case WICON_STORM:
            cloud(x0 + R(0.09f * s), y0 + R(0.04f * s), 0.82f * s, COL_GRAY_D, ink);
            bolt(cx, y0 + R(0.64f * s), s, ink);
            break;

        case WICON_SNOW:
            cloud(x0 + R(0.09f * s), y0 + R(0.06f * s), 0.82f * s, COL_CLOUD, ink);
            snowFlakes(x0, y0 + R(0.78f * s), s, ink);
            break;

        case WICON_MIST:
        {
            // Stacked bars of alternating width.
            int h = R(0.09f * s); if (h < 3) h = 3;
            for (int i = 0; i < 4; i++)
            {
                int w  = R((i & 1) ? 0.62f * s : 0.84f * s);
                int bx = x0 + (size - w) / 2 + ((i & 1) ? R(0.06f * s) : 0);
                int by = y0 + R((0.22f + i * 0.16f) * s);
                g->fillRoundRect(bx - OUTLINE_W, by - OUTLINE_W, w + 2 * OUTLINE_W, h + 2 * OUTLINE_W, h / 2 + OUTLINE_W, ink);
                g->fillRoundRect(bx, by, w, h, h / 2, COL_GRAY);
            }
            break;
        }

        default:
            // Unknown condition: a plain cloud outline, so the slot never looks broken.
            cloudShape(x0 + R(0.05f * s), y0 + R(0.20f * s), 0.90f * s, ink, OUTLINE_W);
            cloudShape(x0 + R(0.05f * s), y0 + R(0.20f * s), 0.90f * s, bg, 0);
            break;
    }
}
