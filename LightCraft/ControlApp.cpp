/*
===========================================================================
Name        : ControlApp.cpp
Author      : Brandon Van Pelt
Description : Control tab (see ControlApp.h).

              Layout, top to bottom:
                Mini-split card   y  56..236   power + setpoint only; the whole
                                               card is a button that opens the
                                               full page (ClimateApp)
                Light row card    y 248..472   three name + toggle pairs

              The split is deliberate: changing the temperature is the daily
              job, so it stays one tap away, and everything else moves to a
              page with room to grow. The space that frees up goes to the
              setpoint readout and to the light toggles.
===========================================================================
*/
#include "ControlApp.h"
#include "RelayControl.h"
#include "TempControl.h"
#include "MiniSplit.h"
#include <App.h>

// --- Button layout ---------------------------------------------------------
// MS_CARD is the card face and must stay the lowest index of the group: it is
// drawn first (so the controls land on top of it), and subMenuButtonMonitor
// keeps the LAST clickable button under the touch, so the controls win a tap
// that lands on them while the rest of the card opens the full page.
enum {
    MS_CARD = 0, MS_TITLE, MS_CHEV, MS_STATUS,
    MS_MINUS, MS_SETPOINT, MS_PLUS,
    MS_MODE_0,
    LIGHT_BASE = MS_MODE_0 + (int)MS_UI_MODE_COUNT,
    CTRL_BTN_COUNT = LIGHT_BASE + (int)LIGHT_COUNT * 2
};

static uint8_t nameIdx(uint8_t i)   { return (uint8_t)(LIGHT_BASE + 2 * i + 0); }
static uint8_t toggleIdx(uint8_t i) { return (uint8_t)(LIGHT_BASE + 2 * i + 1); }

// --- Click returns ---------------------------------------------------------
static const int SW_BASE      = 1;    // lights: 1..3
static const int CR_MS_CARD   = 9;
static const int CR_TEMP_DOWN = 11;
static const int CR_TEMP_UP   = 12;
static const int CR_MODE_BASE = 20;   // 20..23 (off, heat, cool, auto)

static const uint32_t AUTO_RETURN_MS = 30000;   // back to Home after a light on
static const uint16_t COL_ON         = 0x07E0;  // green when a light (or the unit) is on

// Mini-split card geometry.
static const int MSC_X = 24,  MSC_Y = 56;
static const int MSC_W = 432, MSC_H = 210;

static uint32_t s_autoReturnAt = 0;             // 0 == disarmed

// Relay state currently painted, so the page can notice a change made by the
// temperature rules rather than by a tap.
static bool s_shownOn[LIGHT_COUNT];

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

// --- Mini-split ------------------------------------------------------------
static void styleClimate(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    MiniSplitState s = MINISPLIT_get();
    const uint8_t active = MINISPLIT_uiMode();

    b[MS_SETPOINT].setTextFormat("%d\xF8", s.setpointF);

    for (uint8_t m = 0; m < MS_UI_MODE_COUNT; m++)
    {
        UserInterfaceClass& mb = b[MS_MODE_0 + m];
        const bool selected = (m == active);

        mb.setText(MINISPLIT_uiModeName(m));

        // OFF selected reads as "the unit is off", so it takes the same green
        // the light toggles use for on/off state rather than the accent.
        const uint16_t accent = (m == 0) ? gfxTheme.btnBorder : gfxTheme.orangeBtn;
        mb.setBgColor(selected ? accent : gfxTheme.btnColor);
        mb.setBorderColor(selected ? accent : gfxTheme.btnBorder);
        mb.setTextColor(selected ? ((m == 0) ? gfxTheme.btnText : 0x0000) : gfxTheme.btnText);
    }
}

// What the panel is doing, then whether it can reach the blaster node, then the
// fan — the one setting that lives only on the full page.
static void setStatusLabel(void)
{
    UserInterfaceClass& b = GUI_I.appButtons()[MS_STATUS];

    if (MINISPLIT_isPending()) { b.setText("sending..."); return; }
    if (!MINISPLIT_isLinked()) { b.setText("no link");    return; }

    MiniSplitState s = MINISPLIT_get();
    b.setTextFormat("FAN %s", MINISPLIT_fanName(s.fan));
}

// --- Lights ----------------------------------------------------------------
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

    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    // === Mini-split card (tappable) ========================================
    // Shadow from drawCard; the face is the button itself, so the whole card
    // highlights when tapped.
    GUI_I.drawCard(MSC_X, MSC_Y, MSC_W, MSC_H, 20, fill, shadow, 6);

    b[MS_CARD].setButton(MSC_X, MSC_Y, MSC_X + MSC_W, MSC_Y + MSC_H, CR_MS_CARD, true, 20,
                         "", ALIGN_CENTER, fill, fill, gfxTheme.btnBorder, fill);

    b[MS_TITLE].setButton(44, 64, 190, 98, 0, true, 10, "Mini Split", ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
    b[MS_TITLE].setTextSize(16);  b[MS_TITLE].setClickable(false);

    // Link state sits where the power button used to: mode OFF took over that
    // job, so the slot was free for the thing that had nowhere else to go.
    b[MS_STATUS].setButton(200, 64, 390, 98, 0, true, 10, "", ALIGN_CENTER, fill, fill, dim);
    b[MS_STATUS].setTextSize(16); b[MS_STATUS].setClickable(false);

    // Top-right chevron, same cue as the Home weather card: the card opens.
    // Not clickable — the card face underneath takes the tap.
    b[MS_CHEV].setButton(404, 60, 444, 100, 0, true, 10, ">", ALIGN_CENTER,
                         fill, fill, gfxShade(gfxTheme.btnTextColor, -25));
    b[MS_CHEV].setTextSize(24);   b[MS_CHEV].setClickable(false);

    b[MS_MINUS].setButton(44, 114, 134, 188, CR_TEMP_DOWN, true, 16, "-", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_MINUS].setTextSize(32);

    b[MS_SETPOINT].setButton(144, 108, 336, 194, 0, true, 10, "--\xF8", ALIGN_CENTER,
                             fill, fill, gfxTheme.btnTextColor);
    b[MS_SETPOINT].setTextSize(48); b[MS_SETPOINT].setClickable(false);

    b[MS_PLUS].setButton(346, 114, 436, 188, CR_TEMP_UP, true, 16, "+", ALIGN_CENTER,
                         gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[MS_PLUS].setTextSize(32);

    // Mode along the bottom of the card: OFF | HEAT | COOL | AUTO.
    for (uint8_t m = 0; m < MS_UI_MODE_COUNT; m++)
    {
        int x1 = 44 + m * 100;
        b[MS_MODE_0 + m].setButton(x1, 204, x1 + 92, 254, (uint16_t)(CR_MODE_BASE + m), true, 14,
                                   "", ALIGN_CENTER, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[MS_MODE_0 + m].setTextSize(16);
    }

    // === Light row =========================================================
    GUI_I.drawCard(24, 278, 432, 194, 18, fill, shadow, 6);

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        int x1 = 40 + i * 137;
        int x2 = x1 + 125;

        b[nameIdx(i)].setButton(x1, 288, x2, 318, 0, true, 10, "", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
        b[nameIdx(i)].setTextSize(16);
        b[nameIdx(i)].setClickable(false);

        b[toggleIdx(i)].setButton(x1, 324, x2, 462, (uint16_t)(SW_BASE + i), true, 16, "OFF", ALIGN_CENTER,
                                  gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[toggleIdx(i)].setTextSize(32);

        setNameLabel(i);
        styleToggle(i);
    }

    styleClimate();
    setStatusLabel();

    return CTRL_BTN_COUNT;
}

static void refreshClimate(void)
{
    styleClimate();
    setStatusLabel();
    GUI_I.updateButton(MS_SETPOINT);
    GUI_I.updateButton(MS_STATUS);
    for (uint8_t m = 0; m < MS_UI_MODE_COUNT; m++)
        GUI_I.updateButton(MS_MODE_0 + m);
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
    if (userInput == CR_MS_CARD)
    {
        App* app = GUI_I.getApp();
        if (app) app->newApp(APP_CLIMATE);
        return;
    }

    if (userInput == CR_TEMP_DOWN)      MINISPLIT_adjustSetpoint(-1);
    else if (userInput == CR_TEMP_UP)   MINISPLIT_adjustSetpoint(+1);
    else if (userInput >= CR_MODE_BASE && userInput < CR_MODE_BASE + MS_UI_MODE_COUNT)
        MINISPLIT_setUiMode((uint8_t)(userInput - CR_MODE_BASE));
    else                                return;

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

        // Status line: "sending..." clears itself once the frame goes out.
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
