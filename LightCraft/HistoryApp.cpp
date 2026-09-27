/*
===========================================================================
Name        : HistoryApp.cpp
Author      : Brandon Van Pelt
Description : Room temperature history graph (see HistoryApp.h).

              Layout:
                header   y  58..98    back + title
                stats    y 104..136   now / min / max
                plot     y 150..380
                x axis   y 386..410
===========================================================================
*/
#include "HistoryApp.h"
#include "TempHistory.h"
#include "WeatherTime.h"
#include <App.h>
#include <Arduino_GFX_Library.h>

static Arduino_GFX* g = nullptr;

void historyApp_begin(Arduino_GFX* gfx) { g = gfx; }

// --- Button layout ---------------------------------------------------------
enum { IDX_BACK = 0, IDX_TITLE, IDX_STATS, IDX_YMAX, IDX_YMIN,
       IDX_XOLD, IDX_XMID, IDX_XNOW, HIST_BTN_COUNT };

static const int CR_BACK = 1;

// --- Plot geometry ---------------------------------------------------------
static const int PLOT_X0 = 70,  PLOT_X1 = 456;
static const int PLOT_Y0 = 150, PLOT_Y1 = 380;

static uint32_t s_shownUpdates = 0xFFFFFFFFu;

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

// Everything here is in tenths of a degree F, as TempHistory stores it.
//
// The span actually plotted. Auto-scaled to the data, but never narrower than
// this — a steady room would otherwise turn sensor noise into a mountain range.
static const int16_t MIN_SPAN_TENTHS = 60;   // 6.0 F

static void axisRange(int16_t& lo, int16_t& hi)
{
    int16_t dataMin = 0, dataMax = 0;
    if (!TEMPHIST_range(dataMin, dataMax))
    {
        lo = 650; hi = 750;
        return;
    }

    lo = dataMin;
    hi = dataMax;

    int16_t span = (int16_t)(hi - lo);
    if (span < MIN_SPAN_TENTHS)
    {
        const int16_t grow = (int16_t)((MIN_SPAN_TENTHS - span + 1) / 2);
        lo = (int16_t)(lo - grow);
        hi = (int16_t)(hi + grow);
    }
}

static void setLabels(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    int16_t lo, hi;
    axisRange(lo, hi);
    b[IDX_YMAX].setTextFormat("%d\xF8", hi / 10);
    b[IDX_YMIN].setTextFormat("%d\xF8", lo / 10);

    WeatherData w = weather_get();
    int16_t dataMin, dataMax;
    const bool have = TEMPHIST_range(dataMin, dataMax);

    if (w.roomValid && have)
        b[IDX_STATS].setTextFormat("Now %d\xF8   Low %d\xF8   High %d\xF8",
                                   w.roomTempF, dataMin / 10, dataMax / 10);
    else if (w.roomValid)
        b[IDX_STATS].setTextFormat("Now %d\xF8   collecting...", w.roomTempF);
    else
        b[IDX_STATS].setText("Waiting for the room sensor");
}

// Everything inside the plot frame: background, grid, and the trace.
static void drawPlot(void)
{
    if (!g)
        return;

    const uint16_t fill = cardFill();
    const uint16_t grid = gfxShade(fill, -18);
    const int w = PLOT_X1 - PLOT_X0;
    const int h = PLOT_Y1 - PLOT_Y0;

    g->fillRect(PLOT_X0, PLOT_Y0, w, h, fill);

    // Quarter grid lines, and a brighter baseline along the bottom.
    for (int i = 1; i < 4; i++)
        g->drawFastHLine(PLOT_X0, PLOT_Y0 + (h * i) / 4, w, grid);
    g->drawFastHLine(PLOT_X0, PLOT_Y1 - 1, w, gfxShade(fill, -30));

    static int16_t samples[TEMPHIST_SLOTS];
    TEMPHIST_copy(samples, TEMPHIST_SLOTS);

    int16_t lo, hi;
    axisRange(lo, hi);
    const int span = (hi - lo) > 0 ? (hi - lo) : 1;

    // Map a slot to its pixel column, and a reading to its row.
    const uint16_t trace = gfxTheme.orangeBtn;
    int prevX = 0, prevY = 0;
    bool havePrev = false;

    for (uint16_t i = 0; i < TEMPHIST_SLOTS; i++)
    {
        if (samples[i] == TEMPHIST_EMPTY)
        {
            havePrev = false;           // gap: start a new segment after it
            continue;
        }

        const int x = PLOT_X0 + ((int)i * (w - 1)) / (TEMPHIST_SLOTS - 1);
        int y = PLOT_Y1 - 1 - (((int)samples[i] - lo) * (h - 2)) / span;
        if (y < PLOT_Y0)     y = PLOT_Y0;
        if (y > PLOT_Y1 - 1) y = PLOT_Y1 - 1;

        if (havePrev)
        {
            // Two-pixel trace: one line would disappear against the grid.
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

uint8_t historyApp_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t dim = gfxShade(gfxTheme.btnTextColor, -30);

    b[IDX_BACK].setButton(20, 58, 112, 98, CR_BACK, true, 14, "Back", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_BACK].setTextSize(16);

    b[IDX_TITLE].setButton(124, 58, 456, 98, 0, true, 10, "Room history", ALIGN_LEFT,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_TITLE].setTextSize(16);
    b[IDX_TITLE].setClickable(false);

    b[IDX_STATS].setButton(24, 104, 456, 136, 0, true, 10, "", ALIGN_CENTER,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_STATS].setTextSize(16);
    b[IDX_STATS].setClickable(false);

    b[IDX_YMAX].setButton(16, 142, 64, 170, 0, true, 8, "", ALIGN_RIGHT,
                          gfxTheme.background, gfxTheme.background, dim);
    b[IDX_YMAX].setTextSize(16);
    b[IDX_YMAX].setClickable(false);

    b[IDX_YMIN].setButton(16, 360, 64, 388, 0, true, 8, "", ALIGN_RIGHT,
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

    s_shownUpdates = TEMPHIST_updateCount();
    return HIST_BTN_COUNT;
}

void historyApp_handler(int userInput)
{
    if (userInput != CR_BACK)
        return;

    App* app = GUI_I.getApp();
    if (app) app->newApp(APP_HOME);
}

void historyApp_tick(void)
{
    App* app = GUI_I.getApp();

    if (!app || app->getActiveApp() != APP_HISTORY || app->renderState != App::APP_STATE_DONE)
        return;

    if (TEMPHIST_updateCount() == s_shownUpdates)
        return;

    // A new slot closed while the page is open: repaint in place. The plot is
    // drawn straight to the panel, so only the labels go through updateButton.
    setLabels();
    GUI_I.updateButton(IDX_STATS);
    GUI_I.updateButton(IDX_YMAX);
    GUI_I.updateButton(IDX_YMIN);
    drawPlot();
    GUI_I.updateScreen();

    s_shownUpdates = TEMPHIST_updateCount();
}
