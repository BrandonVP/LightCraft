/*
===========================================================================
Name        : ClimateApp.cpp
Author      : Brandon Van Pelt
Description : Full-screen mini-split page (see ClimateApp.h).

              Layout, top to bottom:
                header       y  56..104   back, title, status, power
                setpoint     y 112..228   large readout between - and +
                mode         y 240..288
                fan          y 298..346
                vertical     y 356..404
                horizontal   y 414..462
===========================================================================
*/
#include "ClimateApp.h"
#include "MiniSplit.h"
#include "WeatherTime.h"
#include <App.h>

// --- Button layout ---------------------------------------------------------
enum {
    IDX_BACK = 0, IDX_TITLE, IDX_STATUS, IDX_POWER,
    IDX_MINUS, IDX_SETPOINT, IDX_PLUS,
    IDX_MODE_LABEL,  IDX_MODE_0,
    IDX_FAN_LABEL  = IDX_MODE_0 + (int)MS_MODE_COUNT,
    IDX_FAN_0,
    IDX_VERT_LABEL = IDX_FAN_0 + (int)MS_FAN_COUNT,
    IDX_VERT_0,
    IDX_HORIZ_LABEL = IDX_VERT_0 + (int)MS_SWING_COUNT,
    IDX_HORIZ_0,
    CLIMATE_BTN_COUNT = IDX_HORIZ_0 + (int)MS_SWING_COUNT
};

// --- Click returns ---------------------------------------------------------
static const int CR_BACK        = 1;
static const int CR_POWER       = 10;
static const int CR_TEMP_DOWN   = 11;
static const int CR_TEMP_UP     = 12;
static const int CR_MODE_BASE   = 20;
static const int CR_FAN_BASE    = 30;
static const int CR_VERT_BASE   = 40;
static const int CR_HORIZ_BASE  = 50;

static const uint16_t COL_ON = 0x07E0;   // green, same as the light toggles

static uint16_t cardFill(void) { return gfxShade(gfxTheme.background, 15); }

// --- Styling ---------------------------------------------------------------
static void styleSegment(uint8_t index, bool selected)
{
    UserInterfaceClass& b = GUI_I.appButtons()[index];
    if (selected)
    {
        b.setBgColor(gfxTheme.orangeBtn);
        b.setBorderColor(gfxTheme.orangeBtn);
        b.setTextColor(0x0000);
    }
    else
    {
        b.setBgColor(gfxTheme.btnColor);
        b.setBorderColor(gfxTheme.btnBorder);
        b.setTextColor(gfxTheme.btnText);
    }
}

static void styleAll(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    MiniSplitState s = MINISPLIT_get();

    b[IDX_POWER].setText(s.power ? "ON" : "OFF");
    b[IDX_POWER].setBgColor(s.power ? COL_ON : gfxTheme.btnColor);
    b[IDX_POWER].setBorderColor(s.power ? COL_ON : gfxTheme.btnBorder);
    b[IDX_POWER].setTextColor(s.power ? 0x0000 : gfxTheme.btnText);

    b[IDX_SETPOINT].setTextFormat("%d\xF8", s.setpointF);

    for (uint8_t m = 0; m < MS_MODE_COUNT; m++)
    {
        b[IDX_MODE_0 + m].setText(MINISPLIT_modeName(m));
        styleSegment((uint8_t)(IDX_MODE_0 + m), m == s.mode);
    }

    for (uint8_t f = 0; f < MS_FAN_COUNT; f++)
    {
        b[IDX_FAN_0 + f].setText(MINISPLIT_fanName(f));
        styleSegment((uint8_t)(IDX_FAN_0 + f), f == s.fan);
    }

    for (uint8_t v = 0; v < MS_SWING_COUNT; v++)
    {
        b[IDX_VERT_0 + v].setText(v == MS_SWING_ON ? "SWING" : "FIXED");
        styleSegment((uint8_t)(IDX_VERT_0 + v), v == s.swingV);

        b[IDX_HORIZ_0 + v].setText(v == MS_SWING_ON ? "SWING" : "FIXED");
        styleSegment((uint8_t)(IDX_HORIZ_0 + v), v == s.swingH);
    }
}

static void setStatusLabel(void)
{
    UserInterfaceClass& b = GUI_I.appButtons()[IDX_STATUS];

    if (MINISPLIT_isPending()) { b.setText("sending..."); return; }
    if (!MINISPLIT_isLinked()) { b.setText("no link");    return; }

    WeatherData w = weather_get();
    if (w.roomValid) b.setTextFormat("Room %d\xF8" "F", w.roomTempF);
    else             b.setText("");
}

// --- Page ------------------------------------------------------------------
uint8_t climate_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    // --- Header ------------------------------------------------------------
    b[IDX_BACK].setButton(20, 58, 112, 98, CR_BACK, true, 14, "Back", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_BACK].setTextSize(16);

    b[IDX_TITLE].setButton(124, 56, 330, 80, 0, true, 8, "Mini Split", ALIGN_LEFT,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_TITLE].setTextSize(16);  b[IDX_TITLE].setClickable(false);

    b[IDX_STATUS].setButton(124, 80, 330, 104, 0, true, 8, "", ALIGN_LEFT,
                            gfxTheme.background, gfxTheme.background, dim);
    b[IDX_STATUS].setTextSize(16); b[IDX_STATUS].setClickable(false);

    b[IDX_POWER].setButton(336, 56, 456, 100, CR_POWER, true, 14, "OFF", ALIGN_CENTER,
                           gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_POWER].setTextSize(16);

    // --- Setpoint ----------------------------------------------------------
    GUI_I.drawCard(24, 112, 432, 116, 18, fill, shadow, 5);

    b[IDX_MINUS].setButton(44, 122, 134, 218, CR_TEMP_DOWN, true, 16, "-", ALIGN_CENTER,
                           gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_MINUS].setTextSize(32);

    b[IDX_SETPOINT].setButton(144, 112, 336, 228, 0, true, 10, "--\xF8", ALIGN_CENTER,
                              fill, fill, gfxTheme.btnTextColor);
    b[IDX_SETPOINT].setTextSize(56); b[IDX_SETPOINT].setClickable(false);

    b[IDX_PLUS].setButton(346, 122, 436, 218, CR_TEMP_UP, true, 16, "+", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_PLUS].setTextSize(32);

    // --- Mode --------------------------------------------------------------
    b[IDX_MODE_LABEL].setButton(40, 246, 116, 282, 0, true, 8, "MODE", ALIGN_LEFT,
                                gfxTheme.background, gfxTheme.background, dim);
    b[IDX_MODE_LABEL].setTextSize(16); b[IDX_MODE_LABEL].setClickable(false);

    for (uint8_t m = 0; m < MS_MODE_COUNT; m++)
    {
        int x1 = 122 + m * 106;
        b[IDX_MODE_0 + m].setButton(x1, 240, x1 + 100, 288, (uint16_t)(CR_MODE_BASE + m), true, 14,
                                    "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[IDX_MODE_0 + m].setTextSize(16);
    }

    // --- Fan ---------------------------------------------------------------
    b[IDX_FAN_LABEL].setButton(40, 304, 116, 340, 0, true, 8, "FAN", ALIGN_LEFT,
                               gfxTheme.background, gfxTheme.background, dim);
    b[IDX_FAN_LABEL].setTextSize(16); b[IDX_FAN_LABEL].setClickable(false);

    for (uint8_t f = 0; f < MS_FAN_COUNT; f++)
    {
        int x1 = 122 + f * 80;
        b[IDX_FAN_0 + f].setButton(x1, 298, x1 + 74, 346, (uint16_t)(CR_FAN_BASE + f), true, 14,
                                   "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[IDX_FAN_0 + f].setTextSize(16);
    }

    // --- Blades ------------------------------------------------------------
    b[IDX_VERT_LABEL].setButton(40, 362, 116, 398, 0, true, 8, "VERT", ALIGN_LEFT,
                                gfxTheme.background, gfxTheme.background, dim);
    b[IDX_VERT_LABEL].setTextSize(16); b[IDX_VERT_LABEL].setClickable(false);

    b[IDX_HORIZ_LABEL].setButton(40, 420, 116, 456, 0, true, 8, "HORIZ", ALIGN_LEFT,
                                 gfxTheme.background, gfxTheme.background, dim);
    b[IDX_HORIZ_LABEL].setTextSize(16); b[IDX_HORIZ_LABEL].setClickable(false);

    for (uint8_t v = 0; v < MS_SWING_COUNT; v++)
    {
        int x1 = 122 + v * 160;

        b[IDX_VERT_0 + v].setButton(x1, 356, x1 + 154, 404, (uint16_t)(CR_VERT_BASE + v), true, 14,
                                    "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[IDX_VERT_0 + v].setTextSize(16);

        b[IDX_HORIZ_0 + v].setButton(x1, 414, x1 + 154, 462, (uint16_t)(CR_HORIZ_BASE + v), true, 14,
                                     "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[IDX_HORIZ_0 + v].setTextSize(16);
    }

    styleAll();
    setStatusLabel();

    return CLIMATE_BTN_COUNT;
}

static void refreshAll(void)
{
    styleAll();
    setStatusLabel();
    for (int i = IDX_STATUS; i < CLIMATE_BTN_COUNT; i++)
        GUI_I.updateButton(i);
    GUI_I.updateScreen();
}

void climate_handler(int userInput)
{
    if (userInput < 0)
        return;

    if (userInput == CR_BACK)
    {
        App* app = GUI_I.getApp();
        if (app) app->newApp(APP_CONTROL);
        return;
    }

    MiniSplitState s = MINISPLIT_get();

    if (userInput == CR_POWER)          MINISPLIT_setPower(!s.power);
    else if (userInput == CR_TEMP_DOWN) MINISPLIT_adjustSetpoint(-1);
    else if (userInput == CR_TEMP_UP)   MINISPLIT_adjustSetpoint(+1);
    else if (userInput >= CR_MODE_BASE  && userInput < CR_MODE_BASE  + MS_MODE_COUNT)
        MINISPLIT_setMode((uint8_t)(userInput - CR_MODE_BASE));
    else if (userInput >= CR_FAN_BASE   && userInput < CR_FAN_BASE   + MS_FAN_COUNT)
        MINISPLIT_setFan((uint8_t)(userInput - CR_FAN_BASE));
    else if (userInput >= CR_VERT_BASE  && userInput < CR_VERT_BASE  + MS_SWING_COUNT)
        MINISPLIT_setSwingV((uint8_t)(userInput - CR_VERT_BASE));
    else if (userInput >= CR_HORIZ_BASE && userInput < CR_HORIZ_BASE + MS_SWING_COUNT)
        MINISPLIT_setSwingH((uint8_t)(userInput - CR_HORIZ_BASE));
    else
        return;

    refreshAll();
}

void climate_tick(void)
{
    App* app = GUI_I.getApp();

    if (!app || app->getActiveApp() != APP_CLIMATE || app->renderState != App::APP_STATE_DONE)
        return;

    static uint32_t lastMs = 0;
    if (millis() - lastMs < 250)
        return;
    lastMs = millis();

    const char* before = GUI_I.appButtons()[IDX_STATUS].getBtnText();
    char previous[32];
    strncpy(previous, before ? before : "", sizeof(previous) - 1);
    previous[sizeof(previous) - 1] = '\0';

    setStatusLabel();
    if (strcmp(previous, GUI_I.appButtons()[IDX_STATUS].getBtnText()) != 0)
    {
        GUI_I.updateButton(IDX_STATUS);
        GUI_I.updateScreen();
    }
}
