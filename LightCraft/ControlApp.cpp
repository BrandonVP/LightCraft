/*
===========================================================================
Name        : ControlApp.cpp
Author      : Brandon Van Pelt
Description : Control tab (see ControlApp.h).

              Layout, top to bottom:
                Mini-split card   y  56..370   power, setpoint, mode, fan, swing
                Light row card    y 378..472   three compact name + toggle pairs
===========================================================================
*/
#include "ControlApp.h"
#include "RelayControl.h"
#include "TempControl.h"
#include "MiniSplit.h"
#include "WeatherTime.h"
#include <App.h>

// --- Button layout ---------------------------------------------------------
enum {
    MS_TITLE = 0, MS_STATUS, MS_POWER,
    MS_MINUS, MS_SETPOINT, MS_PLUS,
    MS_MODE_LABEL, MS_MODE_0, MS_MODE_1, MS_MODE_2,
    MS_FAN_LABEL,  MS_FAN_0, MS_FAN_1, MS_FAN_2, MS_FAN_3,
    MS_SWING_V, MS_SWING_H,
    LIGHT_BASE,
    CTRL_BTN_COUNT = LIGHT_BASE + (int)LIGHT_COUNT * 2
};

static uint8_t nameIdx(uint8_t i)   { return (uint8_t)(LIGHT_BASE + 2 * i + 0); }
static uint8_t toggleIdx(uint8_t i) { return (uint8_t)(LIGHT_BASE + 2 * i + 1); }

static uint8_t modeIdx(uint8_t m)   { return (uint8_t)(MS_MODE_0 + m); }
static uint8_t fanIdx(uint8_t f)    { return (uint8_t)(MS_FAN_0 + f); }

// --- Click returns ---------------------------------------------------------
static const int SW_BASE        = 1;    // lights: 1..3
static const int CR_POWER       = 10;
static const int CR_TEMP_DOWN   = 11;
static const int CR_TEMP_UP     = 12;
static const int CR_MODE_BASE   = 20;   // 20..22
static const int CR_FAN_BASE    = 30;   // 30..33
static const int CR_SWING_V     = 40;
static const int CR_SWING_H     = 41;

static const uint32_t AUTO_RETURN_MS = 30000;   // back to Home after a light on
static const uint16_t COL_ON         = 0x07E0;  // green when a light (or the unit) is on

static uint32_t s_autoReturnAt = 0;             // 0 == disarmed

// Relay state currently painted, so the page can notice a change made by the
// temperature rules rather than by a tap.
static bool s_shownOn[LIGHT_COUNT];

static uint16_t cardFill(void) { return gfxShade(gfxTheme.background, 15); }

// --- Mini-split styling ----------------------------------------------------
// One selected look across every segmented row: the words carry the meaning,
// the accent just says "this is the one".
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

static void styleClimate(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    MiniSplitState s = MINISPLIT_get();

    b[MS_POWER].setText(s.power ? "ON" : "OFF");
    b[MS_POWER].setBgColor(s.power ? COL_ON : gfxTheme.btnColor);
    b[MS_POWER].setBorderColor(s.power ? COL_ON : gfxTheme.btnBorder);
    b[MS_POWER].setTextColor(s.power ? 0x0000 : gfxTheme.btnText);

    b[MS_SETPOINT].setTextFormat("%d\xF8", s.setpointF);

    for (uint8_t m = 0; m < MS_MODE_COUNT; m++)
    {
        b[modeIdx(m)].setText(MINISPLIT_modeName(m));
        styleSegment(modeIdx(m), m == s.mode);
    }

    for (uint8_t f = 0; f < MS_FAN_COUNT; f++)
    {
        b[fanIdx(f)].setText(MINISPLIT_fanName(f));
        styleSegment(fanIdx(f), f == s.fan);
    }

    b[MS_SWING_V].setText((s.swingV == MS_SWING_ON) ? "VERT SWING" : "VERT FIXED");
    styleSegment(MS_SWING_V, s.swingV == MS_SWING_ON);

    b[MS_SWING_H].setText((s.swingH == MS_SWING_ON) ? "HORIZ SWING" : "HORIZ FIXED");
    styleSegment(MS_SWING_H, s.swingH == MS_SWING_ON);
}

// Status line: what the panel is doing, then whether it can reach the blaster
// node at all, then the room reading.
static void setStatusLabel(void)
{
    UserInterfaceClass& b = GUI_I.appButtons()[MS_STATUS];

    if (MINISPLIT_isPending())
    {
        b.setText("sending...");
        return;
    }
    if (!MINISPLIT_isLinked())
    {
        b.setText("no link");
        return;
    }

    WeatherData w = weather_get();
    if (w.roomValid) b.setTextFormat("Room %d\xF8", w.roomTempF);
    else             b.setText("");
}

// --- Light styling ---------------------------------------------------------
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
// (e.g. "Fan >74°"), so the tab shows what is automated.
static void setNameLabel(uint8_t i)
{
    UserInterfaceClass& n = GUI_I.appButtons()[nameIdx(i)];
    TempRule r = TEMPCTL_get(i);

    if (r.mode == TEMP_MODE_ABOVE)      n.setTextFormat("%s >%d\xF8", RELAY_name(i), r.setpointF);
    else if (r.mode == TEMP_MODE_BELOW) n.setTextFormat("%s <%d\xF8", RELAY_name(i), r.setpointF);
    else                                n.setText(RELAY_name(i));
}

// --- Page ------------------------------------------------------------------
uint8_t control_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    // Body is cleared to the solid theme background by the framework; depth
    // comes from the raised cards + drop shadows.
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    // === Mini-split card ===================================================
    GUI_I.drawCard(24, 56, 432, 314, 20, fill, shadow, 6);

    b[MS_TITLE].setButton(44, 64, 180, 96, 0, true, 10, "Mini Split", ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
    b[MS_TITLE].setTextSize(16);  b[MS_TITLE].setClickable(false);

    b[MS_STATUS].setButton(186, 64, 330, 96, 0, true, 10, "", ALIGN_CENTER, fill, fill, dim);
    b[MS_STATUS].setTextSize(16); b[MS_STATUS].setClickable(false);

    b[MS_POWER].setButton(340, 60, 438, 104, CR_POWER, true, 14, "OFF", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_POWER].setTextSize(16);

    // Setpoint
    b[MS_MINUS].setButton(44, 112, 124, 186, CR_TEMP_DOWN, true, 16, "-", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_MINUS].setTextSize(24);

    b[MS_SETPOINT].setButton(134, 112, 346, 186, 0, true, 10, "--\xF8", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[MS_SETPOINT].setTextSize(40); b[MS_SETPOINT].setClickable(false);

    b[MS_PLUS].setButton(356, 112, 436, 186, CR_TEMP_UP, true, 16, "+", ALIGN_CENTER,
                         gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_PLUS].setTextSize(24);

    // Mode row
    b[MS_MODE_LABEL].setButton(44, 200, 106, 242, 0, true, 10, "MODE", ALIGN_LEFT, fill, fill, dim);
    b[MS_MODE_LABEL].setTextSize(16); b[MS_MODE_LABEL].setClickable(false);

    for (uint8_t m = 0; m < MS_MODE_COUNT; m++)
    {
        int x1 = 112 + m * 110;
        b[modeIdx(m)].setButton(x1, 196, x1 + 104, 246, (uint16_t)(CR_MODE_BASE + m), true, 14,
                                "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[modeIdx(m)].setTextSize(16);
    }

    // Fan row
    b[MS_FAN_LABEL].setButton(44, 258, 106, 300, 0, true, 10, "FAN", ALIGN_LEFT, fill, fill, dim);
    b[MS_FAN_LABEL].setTextSize(16); b[MS_FAN_LABEL].setClickable(false);

    for (uint8_t f = 0; f < MS_FAN_COUNT; f++)
    {
        int x1 = 112 + f * 82;
        b[fanIdx(f)].setButton(x1, 254, x1 + 76, 304, (uint16_t)(CR_FAN_BASE + f), true, 14,
                               "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[fanIdx(f)].setTextSize(16);
    }

    // Blade movement
    b[MS_SWING_V].setButton(44, 312, 236, 362, CR_SWING_V, true, 14, "", ALIGN_CENTER,
                            gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_SWING_V].setTextSize(16);

    b[MS_SWING_H].setButton(244, 312, 436, 362, CR_SWING_H, true, 14, "", ALIGN_CENTER,
                            gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_SWING_H].setTextSize(16);

    // === Light row =========================================================
    GUI_I.drawCard(24, 378, 432, 94, 16, fill, shadow, 5);

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        int x1 = 40 + i * 137;
        int x2 = x1 + 125;

        b[nameIdx(i)].setButton(x1, 386, x2, 410, 0, true, 10, "", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
        b[nameIdx(i)].setTextSize(16);
        b[nameIdx(i)].setClickable(false);

        b[toggleIdx(i)].setButton(x1, 414, x2, 464, (uint16_t)(SW_BASE + i), true, 14, "OFF", ALIGN_CENTER,
                                  gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[toggleIdx(i)].setTextSize(24);

        setNameLabel(i);
        styleToggle(i);
    }

    styleClimate();
    setStatusLabel();

    return CTRL_BTN_COUNT;
}

// Redraw every mini-split control (a mode change restyles a whole row).
static void refreshClimate(void)
{
    styleClimate();
    setStatusLabel();
    for (int i = MS_TITLE; i < LIGHT_BASE; i++)
        GUI_I.updateButton(i);
    GUI_I.updateScreen();
}

void control_handler(int userInput)
{
    if (userInput < 0)
        return;

    // Any tap counts as "still using this page": push the return-to-Home out so
    // adjusting the mini-split is never interrupted mid-edit.
    if (s_autoReturnAt != 0)
    {
        s_autoReturnAt = millis() + AUTO_RETURN_MS;
        if (s_autoReturnAt == 0) s_autoReturnAt = 1;
    }

    // --- Lights ------------------------------------------------------------
    if (userInput >= SW_BASE && userInput < SW_BASE + LIGHT_COUNT)
    {
        uint8_t i = (uint8_t)(userInput - SW_BASE);
        RELAY_toggle(i);
        styleToggle(i);
        GUI_I.updateButton(toggleIdx(i));
        GUI_I.updateScreen();

        // Turning a light on (re)arms the return-to-Home timer.
        if (RELAY_isOn(i))
        {
            s_autoReturnAt = millis() + AUTO_RETURN_MS;
            if (s_autoReturnAt == 0) s_autoReturnAt = 1;
        }
        return;
    }

    // --- Mini-split --------------------------------------------------------
    MiniSplitState s = MINISPLIT_get();

    if (userInput == CR_POWER)            MINISPLIT_setPower(!s.power);
    else if (userInput == CR_TEMP_DOWN)   MINISPLIT_adjustSetpoint(-1);
    else if (userInput == CR_TEMP_UP)     MINISPLIT_adjustSetpoint(+1);
    else if (userInput == CR_SWING_V)     MINISPLIT_setSwingV(s.swingV == MS_SWING_ON ? MS_SWING_FIXED : MS_SWING_ON);
    else if (userInput == CR_SWING_H)     MINISPLIT_setSwingH(s.swingH == MS_SWING_ON ? MS_SWING_FIXED : MS_SWING_ON);
    else if (userInput >= CR_MODE_BASE && userInput < CR_MODE_BASE + MS_MODE_COUNT)
        MINISPLIT_setMode((uint8_t)(userInput - CR_MODE_BASE));
    else if (userInput >= CR_FAN_BASE && userInput < CR_FAN_BASE + MS_FAN_COUNT)
        MINISPLIT_setFan((uint8_t)(userInput - CR_FAN_BASE));
    else
        return;

    refreshClimate();
}

void control_tick(void)
{
    App* app = GUI_I.getApp();

    if (app && app->getActiveApp() == APP_CONTROL && app->renderState == App::APP_STATE_DONE)
    {
        // Repaint a toggle the temperature rules switched behind our back.
        bool changed = false;
        for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        {
            if (RELAY_isOn(i) == s_shownOn[i])
                continue;

            styleToggle(i);
            GUI_I.updateButton(toggleIdx(i));
            changed = true;
        }

        // Status line: "sending..." clears itself once the frame goes out, and
        // the room reading moves on its own.
        static uint32_t lastStatusMs = 0;
        if (millis() - lastStatusMs >= 250)
        {
            lastStatusMs = millis();

            const char* before = GUI_I.appButtons()[MS_STATUS].getBtnText();
            char previous[32];
            strncpy(previous, before ? before : "", sizeof(previous) - 1);
            previous[sizeof(previous) - 1] = '\0';

            setStatusLabel();
            if (strcmp(previous, GUI_I.appButtons()[MS_STATUS].getBtnText()) != 0)
            {
                GUI_I.updateButton(MS_STATUS);
                changed = true;
            }
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
