/*
===========================================================================
Name        : ForecastApp.cpp
Author      : Brandon Van Pelt
Description : 5-day forecast page (see ForecastApp.h).
===========================================================================
*/
#include "ForecastApp.h"
#include "WeatherTime.h"
#include "WeatherIcons.h"
#include <App.h>

static const char* const DOW[7] =
    { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

// --- Button layout ---------------------------------------------------------
enum { COL_DAY = 0, COL_HIGH, COL_LOW, COL_PER_ROW };

enum {
    BACK_IDX  = 0,
    TITLE_IDX,
    ROW_BASE,
    FC_BTN_COUNT = ROW_BASE + (int)FORECAST_DAYS * (int)COL_PER_ROW
};

static inline uint8_t colIdx(uint8_t day, uint8_t column)
{
    return (uint8_t)(ROW_BASE + day * COL_PER_ROW + column);
}

static const int CR_BACK = 1;

// --- Geometry --------------------------------------------------------------
static const int ROW_Y0     = 106;
static const int ROW_PITCH  = 73;
static const int ROW_HEIGHT = 70;
static const int ICON_CX    = 176;
static const int ICON_SIZE  = 52;

static uint32_t s_shownCount = 0xFFFFFFFFu;   // forecast revision on screen

static uint16_t cardFill(void) { return gfxShade(gfxTheme.background, 15); }

static inline int rowY(uint8_t i) { return ROW_Y0 + i * ROW_PITCH; }

// --- Content (text only; no drawing) ---------------------------------------
static void setTitle(void)
{
    UserInterfaceClass& t = GUI_I.appButtons()[TITLE_IDX];
    t.setText(forecast_isValid() ? "5-Day Forecast" : "Loading forecast...");
}

static void setDayLabels(uint8_t i, const ForecastDay& day)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    if (day.valid)
    {
        // Day 0 is today, whose high/low only cover the hours still to come.
        b[colIdx(i, COL_DAY)].setText((i == 0) ? "Today" : DOW[day.wday % 7]);
        b[colIdx(i, COL_HIGH)].setTextFormat("%d\xF8", day.tempHigh);
        b[colIdx(i, COL_LOW)].setTextFormat("%d\xF8", day.tempLow);
    }
    else
    {
        b[colIdx(i, COL_DAY)].setText("--");
        b[colIdx(i, COL_HIGH)].setText("--");
        b[colIdx(i, COL_LOW)].setText("--");
    }
}

static void drawDayIcon(uint8_t i, const ForecastDay& day)
{
    const int cy = rowY(i) + ROW_HEIGHT / 2;
    if (day.valid) wicon_draw(ICON_CX, cy, ICON_SIZE, wicon_fromOwm(day.iconCode), cardFill());
    else           wicon_clear(ICON_CX, cy, ICON_SIZE, cardFill());
}

// --- Page ------------------------------------------------------------------
uint8_t forecastApp_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    ForecastDay days[FORECAST_DAYS];
    forecast_get(days);

    b[BACK_IDX].setButton(20, 58, 112, 98, CR_BACK, true, 14, "Back", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[BACK_IDX].setTextSize(16);

    b[TITLE_IDX].setButton(124, 58, 460, 98, 0, true, 12, "", ALIGN_CENTER,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[TITLE_IDX].setTextSize(16);
    b[TITLE_IDX].setClickable(false);

    for (uint8_t i = 0; i < FORECAST_DAYS; i++)
    {
        const int y = rowY(i);

        GUI_I.drawCard(16, y, 448, ROW_HEIGHT, 14, fill, shadow, 4);

        b[colIdx(i, COL_DAY)].setButton(32, y + 20, 140, y + 50, 0, true, 10, "--", ALIGN_LEFT,
                                        fill, fill, gfxTheme.btnTextColor);
        b[colIdx(i, COL_DAY)].setTextSize(16);
        b[colIdx(i, COL_DAY)].setClickable(false);

        b[colIdx(i, COL_HIGH)].setButton(250, y + 16, 350, y + 54, 0, true, 10, "--", ALIGN_CENTER,
                                         fill, fill, gfxTheme.btnTextColor);
        b[colIdx(i, COL_HIGH)].setTextSize(24);
        b[colIdx(i, COL_HIGH)].setClickable(false);

        b[colIdx(i, COL_LOW)].setButton(356, y + 20, 446, y + 50, 0, true, 10, "--", ALIGN_CENTER,
                                        fill, fill, dim);
        b[colIdx(i, COL_LOW)].setTextSize(16);
        b[colIdx(i, COL_LOW)].setClickable(false);

        setDayLabels(i, days[i]);

        // Icons are drawn straight onto the card: nothing on this page paints
        // over them, unlike the Home tab's tappable card face.
        drawDayIcon(i, days[i]);
    }

    setTitle();
    s_shownCount = forecast_updateCount();
    return FC_BTN_COUNT;
}

void forecastApp_handler(int userInput)
{
    if (userInput != CR_BACK)
        return;

    App* app = GUI_I.getApp();
    if (app) app->newApp(APP_HOME);
}

void forecastApp_tick(void)
{
    App* app = GUI_I.getApp();

    if (!app || app->getActiveApp() != APP_FORECAST || app->renderState != App::APP_STATE_DONE)
        return;

    if (forecast_updateCount() == s_shownCount)
        return;

    // Repaint in place. Rebuilding the page instead (renderState = INIT) would
    // clear the whole body first, which reads as a black flash a second or two
    // after the page opens — right when the on-demand fetch lands.
    ForecastDay days[FORECAST_DAYS];
    forecast_get(days);

    setTitle();
    GUI_I.updateButton(TITLE_IDX);

    for (uint8_t i = 0; i < FORECAST_DAYS; i++)
    {
        setDayLabels(i, days[i]);
        for (uint8_t c = 0; c < COL_PER_ROW; c++)
            GUI_I.updateButton(colIdx(i, c));
        drawDayIcon(i, days[i]);
    }

    GUI_I.updateScreen();
    s_shownCount = forecast_updateCount();
}
