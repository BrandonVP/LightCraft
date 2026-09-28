/*
===========================================================================
Name        : NetGraphApp.cpp
Author      : Brandon Van Pelt
Description : Internet reachability graph (see NetGraphApp.h).
===========================================================================
*/
#include "NetGraphApp.h"
#include "NetHistory.h"
#include <App.h>
#include <Arduino_GFX_Library.h>

static Arduino_GFX* g = nullptr;

void netGraph_begin(Arduino_GFX* gfx) { g = gfx; }

// --- Button layout ---------------------------------------------------------
enum { IDX_BACK = 0, IDX_TITLE, IDX_STATS, IDX_YMAX, IDX_YMIN,
       IDX_XOLD, IDX_XMID, IDX_XNOW, NET_BTN_COUNT };

static const int CR_BACK = 1;

// --- Plot geometry ---------------------------------------------------------
static const int PLOT_X0 = 70,  PLOT_X1 = 456;
static const int PLOT_Y0 = 150, PLOT_Y1 = 380;

// Latency axis always starts at zero — unlike a temperature, the floor is
// meaningful — and never tops out below this, so a good day is a flat line near
// the bottom rather than noise magnified to fill the panel.
static const int16_t MIN_TOP_MS = 100;

static const uint16_t COL_OUTAGE = 0xF800;   // red, behind the trace

static uint32_t s_shownUpdates = 0xFFFFFFFFu;

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

static int16_t axisTop(void)
{
    int16_t lo = 0, hi = 0, avg = 0;
    uint16_t bad = 0;
    if (!NETHIST_stats(lo, hi, avg, bad) || hi < MIN_TOP_MS)
        return MIN_TOP_MS;

    // Round up to a tidy 50 ms so the label is readable.
    return (int16_t)(((hi + 49) / 50) * 50);
}

static void setLabels(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    b[IDX_YMAX].setTextFormat("%dms", axisTop());
    b[IDX_YMIN].setText("0");

    int16_t lo = 0, hi = 0, avg = 0;
    uint16_t bad = 0;
    const bool have = NETHIST_stats(lo, hi, avg, bad);

    char now[12];
    switch (NETHIST_lastState())
    {
        case NET_PROBE_OK:      snprintf(now, sizeof(now), "%dms", NETHIST_lastLatency()); break;
        case NET_PROBE_NO_WIFI: snprintf(now, sizeof(now), "no wifi");                     break;
        default:                snprintf(now, sizeof(now), "no route");                    break;
    }

    if (have)
        b[IDX_STATS].setTextFormat("Now %s   Avg %dms   Down %um",
                                   now, avg, (unsigned)(bad * 5));
    else
        b[IDX_STATS].setTextFormat("Now %s   collecting...", now);
}

static void drawPlot(void)
{
    if (!g)
        return;

    const uint16_t fill = cardFill();
    const uint16_t grid = gfxShade(fill, -18);
    const int w = PLOT_X1 - PLOT_X0;
    const int h = PLOT_Y1 - PLOT_Y0;

    g->fillRect(PLOT_X0, PLOT_Y0, w, h, fill);

    for (int i = 1; i < 4; i++)
        g->drawFastHLine(PLOT_X0, PLOT_Y0 + (h * i) / 4, w, grid);
    g->drawFastHLine(PLOT_X0, PLOT_Y1 - 1, w, gfxShade(fill, -30));

    static NetSlot slots[NETHIST_SLOTS];
    NETHIST_copy(slots, NETHIST_SLOTS);

    const int top = axisTop();

    // Outage bars first, so the latency trace lands on top of them.
    for (uint16_t i = 0; i < NETHIST_SLOTS; i++)
    {
        if (slots[i].failures == 0)
            continue;
        const int x = PLOT_X0 + ((int)i * (w - 1)) / (NETHIST_SLOTS - 1);
        g->drawFastVLine(x, PLOT_Y0, h - 1, COL_OUTAGE);
    }

    const uint16_t trace = gfxTheme.orangeBtn;
    int prevX = 0, prevY = 0;
    bool havePrev = false;

    for (uint16_t i = 0; i < NETHIST_SLOTS; i++)
    {
        if (slots[i].latencyMs == NETHIST_NO_DATA)
        {
            havePrev = false;           // nothing got through: break the line
            continue;
        }

        const int x = PLOT_X0 + ((int)i * (w - 1)) / (NETHIST_SLOTS - 1);
        int y = PLOT_Y1 - 1 - (((int)slots[i].latencyMs) * (h - 2)) / top;
        if (y < PLOT_Y0)     y = PLOT_Y0;
        if (y > PLOT_Y1 - 1) y = PLOT_Y1 - 1;

        if (havePrev)
        {
            g->drawLine(prevX, prevY, x, y, trace);
            g->drawLine(prevX, prevY + 1, x, y + 1, trace);
        }
        else
        {
            g->drawPixel(x, y, trace);
        }

        prevX = x;
        prevY = y;
        havePrev = true;
    }
}

uint8_t netGraph_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t dim = gfxShade(gfxTheme.btnTextColor, -30);

    b[IDX_BACK].setButton(20, 58, 112, 98, CR_BACK, true, 14, "Back", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_BACK].setTextSize(16);

    b[IDX_TITLE].setButton(124, 58, 456, 98, 0, true, 10, "Connection", ALIGN_LEFT,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_TITLE].setTextSize(16);
    b[IDX_TITLE].setClickable(false);

    b[IDX_STATS].setButton(24, 104, 456, 136, 0, true, 10, "", ALIGN_CENTER,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_STATS].setTextSize(16);
    b[IDX_STATS].setClickable(false);

    b[IDX_YMAX].setButton(8, 142, 64, 170, 0, true, 8, "", ALIGN_RIGHT,
                          gfxTheme.background, gfxTheme.background, dim);
    b[IDX_YMAX].setTextSize(16);
    b[IDX_YMAX].setClickable(false);

    b[IDX_YMIN].setButton(8, 360, 64, 388, 0, true, 8, "0", ALIGN_RIGHT,
                          gfxTheme.background, gfxTheme.background, dim);
    b[IDX_YMIN].setTextSize(16);
    b[IDX_YMIN].setClickable(false);

    b[IDX_XOLD].setButton(66, 386, 160, 412, 0, true, 8, "-24h", ALIGN_LEFT,
                          gfxTheme.background, gfxTheme.background, dim);
    b[IDX_XOLD].setTextSize(16);
    b[IDX_XOLD].setClickable(false);

    b[IDX_XMID].setButton(216, 386, 310, 412, 0, true, 8, "-12h", ALIGN_CENTER,
                          gfxTheme.background, gfxTheme.background, dim);
    b[IDX_XMID].setTextSize(16);
    b[IDX_XMID].setClickable(false);

    b[IDX_XNOW].setButton(370, 386, 458, 412, 0, true, 8, "now", ALIGN_RIGHT,
                          gfxTheme.background, gfxTheme.background, dim);
    b[IDX_XNOW].setTextSize(16);
    b[IDX_XNOW].setClickable(false);

    setLabels();
    drawPlot();

    s_shownUpdates = NETHIST_updateCount();
    return NET_BTN_COUNT;
}

void netGraph_handler(int userInput)
{
    if (userInput != CR_BACK)
        return;

    App* app = GUI_I.getApp();
    if (app) app->newApp(APP_WIFI);
}

void netGraph_tick(void)
{
    App* app = GUI_I.getApp();

    if (!app || app->getActiveApp() != APP_NETGRAPH || app->renderState != App::APP_STATE_DONE)
        return;

    // The status line moves with every probe, not just when a slot closes, so
    // refresh it on a timer and redraw the plot only when there is new history.
    static uint32_t lastStatsMs = 0;
    if (millis() - lastStatsMs >= 1000)
    {
        lastStatsMs = millis();

        const char* before = GUI_I.appButtons()[IDX_STATS].getBtnText();
        char previous[40];
        strncpy(previous, before ? before : "", sizeof(previous) - 1);
        previous[sizeof(previous) - 1] = '\0';

        setLabels();
        if (strcmp(previous, GUI_I.appButtons()[IDX_STATS].getBtnText()) != 0)
        {
            GUI_I.updateButton(IDX_STATS);
            GUI_I.updateScreen();
        }
    }

    if (NETHIST_updateCount() == s_shownUpdates)
        return;

    setLabels();
    GUI_I.updateButton(IDX_STATS);
    GUI_I.updateButton(IDX_YMAX);
    drawPlot();
    GUI_I.updateScreen();

    s_shownUpdates = NETHIST_updateCount();
}
