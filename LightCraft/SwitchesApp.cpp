/*
===========================================================================
Name        : SwitchesApp.cpp
Author      : Brandon Van Pelt
Description : Switches tab UI (see SwitchesApp.h). Three columns, each a name
              label above a large ON/OFF toggle that drives a relay.
===========================================================================
*/
#include "SwitchesApp.h"
#include "RelayControl.h"

// Two buttons per light column: a name label and the toggle.
static uint8_t nameIdx(uint8_t i)   { return (uint8_t)(2 * i + 0); }
static uint8_t toggleIdx(uint8_t i) { return (uint8_t)(2 * i + 1); }

static const int      SW_BASE = 1;             // toggle click-returns: 1..3
static const uint32_t AUTO_RETURN_MS = 30000;  // back to Home 30s after a light on
static const uint16_t COL_ON = 0x07E0;         // green when a light is on

static const char* const NAMES[LIGHT_COUNT] = { "Light 1", "Light 2", "Light 3" };

static uint32_t s_autoReturnAt = 0;            // 0 == disarmed

static void styleToggle(uint8_t i)
{
    UserInterfaceClass& t = GUI_I.appButtons()[toggleIdx(i)];
    bool on = RELAY_isOn(i);
    t.setText(on ? "ON" : "OFF");
    t.setBgColor(on ? COL_ON : gfxTheme.btnColor);
    t.setBorderColor(on ? COL_ON : gfxTheme.btnBorder);
    t.setTextColor(on ? 0x0000 : gfxTheme.btnText);
}

uint8_t switches_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        int x1 = 20 + i * 153;   // 3 columns, ~133 wide, 20 gap
        int x2 = x1 + 133;

        b[nameIdx(i)].setButton(x1, 70, x2, 120, 0, true, 10, NAMES[i], ALIGN_CENTER,
                                gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
        b[nameIdx(i)].setClickable(false);
        b[nameIdx(i)].setTextSize(16);

        b[toggleIdx(i)].setButton(x1, 140, x2, 430, SW_BASE + i, true, 14, "OFF", ALIGN_CENTER,
                                  gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[toggleIdx(i)].setTextSize(24);

        styleToggle(i);
    }

    return (uint8_t)(LIGHT_COUNT * 2);
}

void switches_handler(int userInput)
{
    if (userInput < SW_BASE || userInput >= SW_BASE + LIGHT_COUNT)
        return;

    uint8_t i = (uint8_t)(userInput - SW_BASE);
    RELAY_toggle(i);
    styleToggle(i);
    GUI_I.updateButton(toggleIdx(i));
    GUI_I.updateScreen();

    // Turning a light on (re)arms the return-to-Home timer.
    if (RELAY_isOn(i))
    {
        s_autoReturnAt = millis() + AUTO_RETURN_MS;
        if (s_autoReturnAt == 0) s_autoReturnAt = 1;   // never the disarmed value
    }
}

void switches_tick(void)
{
    if (s_autoReturnAt == 0)
        return;

    if ((int32_t)(millis() - s_autoReturnAt) >= 0)
    {
        s_autoReturnAt = 0;
        App* app = GUI_I.getApp();
        if (app) app->newApp(APP_HOME);
    }
}
