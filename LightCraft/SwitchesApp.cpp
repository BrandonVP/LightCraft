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
#include "TempControl.h"
#include <App.h>

// Two buttons per light column: a name label and the toggle.
static uint8_t nameIdx(uint8_t i)   { return (uint8_t)(2 * i + 0); }
static uint8_t toggleIdx(uint8_t i) { return (uint8_t)(2 * i + 1); }

static const int      SW_BASE = 1;             // toggle click-returns: 1..3
static const uint32_t AUTO_RETURN_MS = 30000;  // back to Home 30s after a light on
static const uint16_t COL_ON = 0x07E0;         // green when a light is on

static uint32_t s_autoReturnAt = 0;            // 0 == disarmed

// Relay state currently painted on the toggles, so the page can notice a change
// made by the temperature rules rather than by a tap.
static bool s_shownOn[LIGHT_COUNT];

static void styleToggle(uint8_t i)
{
    UserInterfaceClass& t = GUI_I.appButtons()[toggleIdx(i)];
    bool on = RELAY_isOn(i);
    t.setText(on ? "ON" : "OFF");
    t.setBgColor(on ? COL_ON : gfxTheme.btnColor);
    t.setBorderColor(on ? COL_ON : gfxTheme.btnBorder);
    t.setTextColor(on ? 0x0000 : gfxTheme.btnText);
    s_shownOn[i] = on;
}

// Name label: plain light name, plus the temperature rule when one is enabled
// (e.g. "Fan >74°"), so the Switches tab shows what is automated.
static void setNameLabel(uint8_t i)
{
    UserInterfaceClass& n = GUI_I.appButtons()[nameIdx(i)];
    TempRule r = TEMPCTL_get(i);

    if (r.mode == TEMP_MODE_ABOVE)
        n.setTextFormat("%s >%d\xF8", RELAY_name(i), r.setpointF);
    else if (r.mode == TEMP_MODE_BELOW)
        n.setTextFormat("%s <%d\xF8", RELAY_name(i), r.setpointF);
    else
        n.setText(RELAY_name(i));
}

uint8_t switches_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    // Body is cleared to the solid theme background by the framework. A subtle
    // gradient posterizes into visible bands on this panel (RGB565), so the depth
    // comes purely from the raised cards + drop shadows.
    const uint16_t cardFill   = gfxShade(gfxTheme.background, 15);   // slightly raised
    const uint16_t cardShadow = 0x0000;                             // pure black drop shadow

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        int x1 = 20 + i * 153;   // 3 columns, ~133 wide, 20 gap
        int x2 = x1 + 133;

        // Floating card holding this light's label + toggle.
        GUI_I.drawCard(x1 - 4, 60, (x2 - x1) + 8, 378, 18, cardFill, cardShadow, 5);

        // Name label — blends onto the card.
        b[nameIdx(i)].setButton(x1, 74, x2, 120, 0, true, 10, RELAY_name(i), ALIGN_CENTER,
                                cardFill, cardFill, gfxTheme.btnTextColor);
        b[nameIdx(i)].setClickable(false);
        b[nameIdx(i)].setTextSize(16);
        setNameLabel(i);

        // Toggle button sits on the card.
        b[toggleIdx(i)].setButton(x1, 140, x2, 424, SW_BASE + i, true, 16, "OFF", ALIGN_CENTER,
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
    App* app = GUI_I.getApp();

    // Repaint a toggle the temperature rules switched behind our back. Only
    // while this page owns the shared button array and its render has settled.
    if (app && app->getActiveApp() == APP_SWITCHES && app->renderState == App::APP_STATE_DONE)
    {
        bool changed = false;
        for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        {
            if (RELAY_isOn(i) == s_shownOn[i])
                continue;

            styleToggle(i);
            GUI_I.updateButton(toggleIdx(i));
            changed = true;
        }
        if (changed)
            GUI_I.updateScreen();
    }

    if (s_autoReturnAt == 0)
        return;

    if ((int32_t)(millis() - s_autoReturnAt) >= 0)
    {
        s_autoReturnAt = 0;
        if (app) app->newApp(APP_HOME);
    }
}
