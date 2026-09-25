/*
===========================================================================
Name        : TempRuleApp.cpp
Author      : Brandon Van Pelt
Description : Settings > Temp Rules page (see TempRuleApp.h).
===========================================================================
*/
#include "TempRuleApp.h"
#include "TempControl.h"
#include "RelayControl.h"
#include "WeatherTime.h"
#include <App.h>

// --- Button layout ---------------------------------------------------------
// One status line, five buttons per light row, one save button.
enum { CTL_NAME = 0, CTL_MODE, CTL_MINUS, CTL_VALUE, CTL_PLUS, CTL_PER_ROW };

enum {
    STATUS_IDX = 0,
    ROW_BASE   = 1,
    SAVE_IDX   = ROW_BASE + (int)LIGHT_COUNT * (int)CTL_PER_ROW,
    TR_BTN_COUNT
};

static inline uint8_t ctlIdx(uint8_t light, uint8_t control)
{
    return (uint8_t)(ROW_BASE + light * CTL_PER_ROW + control);
}

// --- Click returns ---------------------------------------------------------
static const int CR_MODE_BASE  = 10;   // 10..12
static const int CR_MINUS_BASE = 20;   // 20..22
static const int CR_PLUS_BASE  = 30;   // 30..32
static const int CR_SAVE       = 40;

// --- Geometry --------------------------------------------------------------
static const int ROW_Y0     = 98;    // top of the first row card
static const int ROW_PITCH  = 106;
static const int ROW_HEIGHT = 96;

// --- Press-and-hold repeat -------------------------------------------------
static const uint32_t HOLD_DELAY_MS    = 500;   // before repeat starts
static const uint32_t HOLD_INTERVAL_MS = 120;   // between repeats
static const uint32_t HOLD_FAST_MS     = 2500;  // after this, step 5 at a time

static int      s_holdIdx      = -1;    // app-button index currently pressed
static uint32_t s_holdStartMs  = 0;
static uint32_t s_lastRepeatMs = 0;
static bool     s_repeated     = false; // suppress the extra step on release

// Room reading shown on the status line; tracked so it only redraws on change.
static int16_t s_shownRoomF   = INT16_MIN;
static bool    s_shownRoomOk  = false;

static uint16_t cardFill(void) { return gfxShade(gfxTheme.background, 15); }

// --- Label helpers (set text only, no drawing) -----------------------------
static void setStatusLabel(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    WeatherData w = weather_get();

    if (w.roomValid)
        b[STATUS_IDX].setTextFormat("Room %d\xF8" "F", w.roomTempF);
    else
        b[STATUS_IDX].setTextFormat("Room temperature unavailable");

    s_shownRoomF  = w.roomTempF;
    s_shownRoomOk = w.roomValid;
}

static void styleMode(uint8_t light)
{
    UserInterfaceClass& btn = GUI_I.appButtons()[ctlIdx(light, CTL_MODE)];
    TempRule r = TEMPCTL_get(light);

    btn.setText(TEMPCTL_modeName(r.mode));

    switch (r.mode)
    {
        case TEMP_MODE_ABOVE:   // warm accent: the light comes on as it heats up
            btn.setBgColor(gfxTheme.orangeBtn);
            btn.setBorderColor(gfxTheme.orangeBtn);
            btn.setTextColor(0x0000);
            break;
        case TEMP_MODE_BELOW:   // cool accent
            btn.setBgColor(0x047F);
            btn.setBorderColor(0x047F);
            btn.setTextColor(0xFFFF);
            break;
        default:
            btn.setBgColor(gfxTheme.btnColor);
            btn.setBorderColor(gfxTheme.btnBorder);
            btn.setTextColor(gfxTheme.btnText);
            break;
    }
}

static void setSetpointLabel(uint8_t light)
{
    UserInterfaceClass& btn = GUI_I.appButtons()[ctlIdx(light, CTL_VALUE)];
    TempRule r = TEMPCTL_get(light);
    btn.setTextFormat("%d\xF8", r.setpointF);
    // A disabled rule greys its setpoint so the enabled rows read at a glance.
    btn.setTextColor(r.mode == TEMP_MODE_OFF ? gfxShade(gfxTheme.btnTextColor, -45)
                                             : gfxTheme.btnTextColor);
}

static void styleSave(void)
{
    UserInterfaceClass& btn = GUI_I.appButtons()[SAVE_IDX];
    if (TEMPCTL_isDirty())
    {
        btn.setText("SAVE");
        btn.setBgColor(gfxTheme.orangeBtn);
        btn.setBorderColor(gfxTheme.orangeBtn);
        btn.setTextColor(0x0000);
    }
    else
    {
        btn.setText("SAVED");
        btn.setBgColor(cardFill());
        btn.setBorderColor(gfxTheme.btnBorder);
        btn.setTextColor(gfxShade(gfxTheme.btnTextColor, -35));
    }
}

// --- Page ------------------------------------------------------------------
uint8_t temprule_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;

    // Status line sits on the plain body background, above the row cards.
    b[STATUS_IDX].setButton(16, 54, 464, 92, 0, true, 10, "", ALIGN_CENTER,
                            gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[STATUS_IDX].setTextSize(16);
    b[STATUS_IDX].setClickable(false);

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        const int y0 = ROW_Y0 + i * ROW_PITCH;

        GUI_I.drawCard(16, y0, 448, ROW_HEIGHT, 16, fill, shadow, 5);

        // Light name — blends onto the card.
        b[ctlIdx(i, CTL_NAME)].setButton(28, y0 + 28, 150, y0 + 68, 0, true, 10,
                                         RELAY_name(i), ALIGN_LEFT,
                                         fill, fill, gfxTheme.btnTextColor);
        b[ctlIdx(i, CTL_NAME)].setTextSize(16);
        b[ctlIdx(i, CTL_NAME)].setClickable(false);

        // Mode: OFF / ABOVE / BELOW (cycled by tapping).
        b[ctlIdx(i, CTL_MODE)].setButton(156, y0 + 18, 268, y0 + 78,
                                         (uint16_t)(CR_MODE_BASE + i), true, 14, "OFF", ALIGN_CENTER,
                                         gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[ctlIdx(i, CTL_MODE)].setTextSize(16);

        b[ctlIdx(i, CTL_MINUS)].setButton(276, y0 + 18, 328, y0 + 78,
                                          (uint16_t)(CR_MINUS_BASE + i), true, 14, "-", ALIGN_CENTER,
                                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[ctlIdx(i, CTL_MINUS)].setTextSize(24);

        // Setpoint readout — a label on the card, not a button.
        b[ctlIdx(i, CTL_VALUE)].setButton(332, y0 + 24, 396, y0 + 72, 0, true, 10, "--", ALIGN_CENTER,
                                          fill, fill, gfxTheme.btnTextColor);
        b[ctlIdx(i, CTL_VALUE)].setTextSize(24);
        b[ctlIdx(i, CTL_VALUE)].setClickable(false);

        b[ctlIdx(i, CTL_PLUS)].setButton(404, y0 + 18, 452, y0 + 78,
                                         (uint16_t)(CR_PLUS_BASE + i), true, 14, "+", ALIGN_CENTER,
                                         gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[ctlIdx(i, CTL_PLUS)].setTextSize(24);

        styleMode(i);
        setSetpointLabel(i);
    }

    b[SAVE_IDX].setButton(140, 418, 340, 468, CR_SAVE, true, 16, "SAVE", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[SAVE_IDX].setTextSize(16);

    setStatusLabel();
    styleSave();

    // A page rebuild (theme change, tab re-entry) cancels any in-flight hold.
    s_holdIdx  = -1;
    s_repeated = false;

    return TR_BTN_COUNT;
}

// Redraw one row's mode + setpoint and the save button after an edit.
static void refreshRow(uint8_t light)
{
    styleMode(light);
    setSetpointLabel(light);
    styleSave();

    GUI_I.updateButton(ctlIdx(light, CTL_MODE));
    GUI_I.updateButton(ctlIdx(light, CTL_VALUE));
    GUI_I.updateButton(SAVE_IDX);
    GUI_I.updateScreen();
}

static void stepSetpoint(uint8_t light, int16_t delta)
{
    TempRule r = TEMPCTL_get(light);
    TEMPCTL_setSetpoint(light, (int16_t)(r.setpointF + delta));
}

void temprule_handler(int userInput)
{
    if (userInput < 0)
        return;

    if (userInput == CR_SAVE)
    {
        if (TEMPCTL_isDirty())
            TEMPCTL_save();
        styleSave();
        GUI_I.updateButton(SAVE_IDX);
        GUI_I.updateScreen();
        return;
    }

    if (userInput >= CR_MODE_BASE && userInput < CR_MODE_BASE + LIGHT_COUNT)
    {
        uint8_t i = (uint8_t)(userInput - CR_MODE_BASE);
        TempRule r = TEMPCTL_get(i);
        TEMPCTL_setMode(i, (uint8_t)((r.mode + 1) % TEMP_MODE_COUNT));
        refreshRow(i);
        return;
    }

    if (userInput >= CR_MINUS_BASE && userInput < CR_MINUS_BASE + LIGHT_COUNT)
    {
        uint8_t i = (uint8_t)(userInput - CR_MINUS_BASE);
        // The release that ends a hold must not add one more step.
        if (s_repeated) { s_repeated = false; return; }
        stepSetpoint(i, -TEMPCTL_STEP_F);
        refreshRow(i);
        return;
    }

    if (userInput >= CR_PLUS_BASE && userInput < CR_PLUS_BASE + LIGHT_COUNT)
    {
        uint8_t i = (uint8_t)(userInput - CR_PLUS_BASE);
        if (s_repeated) { s_repeated = false; return; }
        stepSetpoint(i, TEMPCTL_STEP_F);
        refreshRow(i);
        return;
    }
}

// Map a pressed app-button index back to {light, direction}; false if the held
// button is not a -/+ step button.
static bool stepButtonAt(int buttonIndex, uint8_t& light, int16_t& sign)
{
    if (buttonIndex < ROW_BASE || buttonIndex >= SAVE_IDX)
        return false;

    int rel     = buttonIndex - ROW_BASE;
    int control = rel % CTL_PER_ROW;
    if (control != CTL_MINUS && control != CTL_PLUS)
        return false;

    light = (uint8_t)(rel / CTL_PER_ROW);
    sign  = (control == CTL_PLUS) ? 1 : -1;
    return light < LIGHT_COUNT;
}

void temprule_tick(void)
{
    App* app = GUI_I.getApp();

    // Only touch the shared button array while this page owns it and its render
    // has settled (same guard the Home tab uses).
    if (!app || app->getActiveApp() != APP_TEMP_RULES || app->renderState != App::APP_STATE_DONE)
    {
        s_holdIdx = -1;
        return;
    }

    // --- Press-and-hold repeat on the -/+ buttons --------------------------
    // activeBodyButtonIndex is set by the GUI on press and cleared on release,
    // so it doubles as "which button is being held right now".
    int active = GUI_I.activeBodyButtonIndex;
    if (active != s_holdIdx)
    {
        s_holdIdx      = active;
        s_holdStartMs  = millis();
        s_lastRepeatMs = 0;
        s_repeated     = false;
    }

    uint8_t light;
    int16_t sign;
    if (active >= 0 && stepButtonAt(active, light, sign))
    {
        uint32_t held = millis() - s_holdStartMs;
        if (held >= HOLD_DELAY_MS && (millis() - s_lastRepeatMs) >= HOLD_INTERVAL_MS)
        {
            s_lastRepeatMs = millis();
            s_repeated     = true;
            stepSetpoint(light, (int16_t)(sign * (held >= HOLD_FAST_MS ? 5 : TEMPCTL_STEP_F)));
            refreshRow(light);
        }
    }

    // --- Live room readout -------------------------------------------------
    WeatherData w = weather_get();
    if (w.roomValid != s_shownRoomOk || (w.roomValid && w.roomTempF != s_shownRoomF))
    {
        setStatusLabel();
        GUI_I.updateButton(STATUS_IDX);
        GUI_I.updateScreen();
    }
}
