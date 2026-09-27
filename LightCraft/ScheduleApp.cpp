/*
===========================================================================
Name        : ScheduleApp.cpp
Author      : Brandon Van Pelt
Description : Settings > Schedules, and the per-light editor (see the header).
===========================================================================
*/
#include "ScheduleApp.h"
#include "Schedule.h"
#include "RelayControl.h"
#include <App.h>

static uint8_t s_editLight = 0;    // which light the editor is showing

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

// ===========================================================================
//  List page
// ===========================================================================
enum { L_FACE = 0, L_NAME, L_SUMMARY, L_CHEV, L_PER_ROW };

enum { L_HEADING = 0, L_ROW0, L_BTN_COUNT = L_ROW0 + (int)LIGHT_COUNT * (int)L_PER_ROW };

static inline uint8_t listIdx(uint8_t row, uint8_t col)
{
    return (uint8_t)(L_ROW0 + row * L_PER_ROW + col);
}

static const int L_ROW_Y0    = 104;
static const int L_ROW_PITCH = 100;
static const int L_ROW_H     = 90;

static const int CR_ROW_BASE = 1;   // 1..3

uint8_t schedule_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    b[L_HEADING].setButton(24, 56, 456, 92, 0, true, 10, "Schedules", ALIGN_LEFT,
                           gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[L_HEADING].setTextSize(16);
    b[L_HEADING].setClickable(false);

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        const int y = L_ROW_Y0 + i * L_ROW_PITCH;

        // Shadow from drawCard, face from the button, so the whole row responds
        // to a tap. The face must stay the lowest index of the row: it is drawn
        // first, and the labels over it are not clickable.
        GUI_I.drawCard(24, y, 432, L_ROW_H, 16, fill, shadow, 5);

        b[listIdx(i, L_FACE)].setButton(24, y, 456, y + L_ROW_H, (uint16_t)(CR_ROW_BASE + i),
                                        true, 16, "", ALIGN_CENTER,
                                        fill, fill, gfxTheme.btnBorder, fill);

        b[listIdx(i, L_NAME)].setButton(44, y + 14, 240, y + 44, 0, true, 10,
                                        RELAY_name(i), ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
        b[listIdx(i, L_NAME)].setTextSize(16);
        b[listIdx(i, L_NAME)].setClickable(false);

        char summary[32];
        SCHED_summary(i, summary, sizeof(summary));
        b[listIdx(i, L_SUMMARY)].setButton(44, y + 48, 400, y + 78, 0, true, 10,
                                           "", ALIGN_LEFT, fill, fill, dim);
        b[listIdx(i, L_SUMMARY)].setTextFormat("%.28s", summary);
        b[listIdx(i, L_SUMMARY)].setTextSize(16);
        b[listIdx(i, L_SUMMARY)].setClickable(false);

        b[listIdx(i, L_CHEV)].setButton(408, y + 26, 446, y + 66, 0, true, 10, ">", ALIGN_CENTER,
                                        fill, fill, gfxShade(gfxTheme.btnTextColor, -25));
        b[listIdx(i, L_CHEV)].setTextSize(24);
        b[listIdx(i, L_CHEV)].setClickable(false);
    }

    return L_BTN_COUNT;
}

void schedule_handler(int userInput)
{
    if (userInput < CR_ROW_BASE || userInput >= CR_ROW_BASE + LIGHT_COUNT)
        return;

    s_editLight = (uint8_t)(userInput - CR_ROW_BASE);

    App* app = GUI_I.getApp();
    if (app) app->newApp(APP_SCHED_EDIT);
}

// ===========================================================================
//  Editor
// ===========================================================================
// Per card: a heading, the trigger selector, and two rows of controls whose
// meaning depends on the trigger — hour/minute for a clock time, offset plus a
// resolved-time readout for sunrise or sunset.
enum { C_LABEL = 0, C_TRIGGER,
       C_A_LABEL, C_A_MINUS, C_A_VALUE, C_A_PLUS,
       C_B_LABEL, C_B_MINUS, C_B_VALUE, C_B_PLUS,
       C_PER_CARD };

enum { E_BACK = 0, E_TITLE,
       E_ON_BASE,
       E_OFF_BASE = E_ON_BASE + (int)C_PER_CARD,
       E_BTN_COUNT = E_OFF_BASE + (int)C_PER_CARD };

static inline uint8_t edIdx(bool onCard, uint8_t col)
{
    return (uint8_t)((onCard ? E_ON_BASE : E_OFF_BASE) + col);
}

static const int CR_BACK        = 1;
static const int CR_ON_TRIGGER  = 10, CR_ON_A_MINUS  = 11, CR_ON_A_PLUS  = 12,
                 CR_ON_B_MINUS  = 13, CR_ON_B_PLUS   = 14;
static const int CR_OFF_TRIGGER = 20, CR_OFF_A_MINUS = 21, CR_OFF_A_PLUS = 22,
                 CR_OFF_B_MINUS = 23, CR_OFF_B_PLUS  = 24;

static const int E_CARD_H  = 170;
static const int E_ON_Y    = 112;
static const int E_OFF_Y   = 294;

static int cardTop(bool onCard) { return onCard ? E_ON_Y : E_OFF_Y; }

static void setVisible(uint8_t index, bool printable, bool clickable)
{
    UserInterfaceClass& btn = GUI_I.appButtons()[index];
    btn.setPrintable(printable);
    btn.setClickable(clickable);
}

// Labels, values and which controls apply, for one card's current trigger.
static void applyCard(bool onCard)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    LightSchedule s = SCHED_get(s_editLight);
    const SchedEvent& e = onCard ? s.on : s.off;

    b[edIdx(onCard, C_TRIGGER)].setText(SCHED_triggerName(e.trigger));
    const bool enabled = (e.trigger != SCHED_NONE);
    b[edIdx(onCard, C_TRIGGER)].setBgColor(enabled ? gfxTheme.orangeBtn : gfxTheme.btnColor);
    b[edIdx(onCard, C_TRIGGER)].setBorderColor(enabled ? gfxTheme.orangeBtn : gfxTheme.btnBorder);
    b[edIdx(onCard, C_TRIGGER)].setTextColor(enabled ? 0x0000 : gfxTheme.btnText);

    bool aShown = false, bLabel = false, bControls = false;

    if (e.trigger == SCHED_TIME)
    {
        aShown = bLabel = bControls = true;

        b[edIdx(onCard, C_A_LABEL)].setText("Hour");
        b[edIdx(onCard, C_A_VALUE)].setTextFormat("%02u", e.hour);
        b[edIdx(onCard, C_A_VALUE)].setTextSize(24);

        b[edIdx(onCard, C_B_LABEL)].setText("Minute");
        b[edIdx(onCard, C_B_VALUE)].setTextFormat("%02u", e.minute);
        b[edIdx(onCard, C_B_VALUE)].setTextSize(24);
    }
    else if (e.trigger == SCHED_SUNRISE || e.trigger == SCHED_SUNSET)
    {
        aShown = bLabel = true;          // offset row, then a read-only readout

        const int mins = (int)e.offsetQ * 15;
        b[edIdx(onCard, C_A_LABEL)].setText("Offset");
        if (mins == 0) b[edIdx(onCard, C_A_VALUE)].setText("0m");
        else           b[edIdx(onCard, C_A_VALUE)].setTextFormat("%+dm", mins);
        b[edIdx(onCard, C_A_VALUE)].setTextSize(16);   // "+120m" will not fit at 24

        // What that actually works out to today, which is the whole point of
        // picking sunset over a clock time.
        const int resolved = SCHED_resolveMinutes(e);
        if (resolved >= 0)
            b[edIdx(onCard, C_B_LABEL)].setTextFormat("today: %02d:%02d", resolved / 60, resolved % 60);
        else
            b[edIdx(onCard, C_B_LABEL)].setText("today: --:--");
    }

    setVisible(edIdx(onCard, C_A_LABEL), aShown, false);
    setVisible(edIdx(onCard, C_A_MINUS), aShown, aShown);
    setVisible(edIdx(onCard, C_A_VALUE), aShown, false);
    setVisible(edIdx(onCard, C_A_PLUS),  aShown, aShown);

    setVisible(edIdx(onCard, C_B_LABEL), bLabel, false);
    setVisible(edIdx(onCard, C_B_MINUS), bControls, bControls);
    setVisible(edIdx(onCard, C_B_VALUE), bControls, false);
    setVisible(edIdx(onCard, C_B_PLUS),  bControls, bControls);
}

static void buildCard(bool onCard)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill = cardFill();
    const uint16_t dim  = gfxShade(gfxTheme.btnTextColor, -30);
    const int Y = cardTop(onCard);

    b[edIdx(onCard, C_LABEL)].setButton(44, Y + 14, 210, Y + 48, 0, true, 10,
                                        onCard ? "Turn on" : "Turn off", ALIGN_LEFT,
                                        fill, fill, gfxTheme.btnTextColor);
    b[edIdx(onCard, C_LABEL)].setTextSize(16);
    b[edIdx(onCard, C_LABEL)].setClickable(false);

    b[edIdx(onCard, C_TRIGGER)].setButton(220, Y + 8, 452, Y + 52,
                                          (uint16_t)(onCard ? CR_ON_TRIGGER : CR_OFF_TRIGGER),
                                          true, 14, "Off", ALIGN_CENTER,
                                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[edIdx(onCard, C_TRIGGER)].setTextSize(16);

    struct { uint8_t label, minus, value, plus; int top; int crMinus, crPlus; } rows[2] = {
        { C_A_LABEL, C_A_MINUS, C_A_VALUE, C_A_PLUS, Y + 62,
          onCard ? CR_ON_A_MINUS : CR_OFF_A_MINUS, onCard ? CR_ON_A_PLUS : CR_OFF_A_PLUS },
        { C_B_LABEL, C_B_MINUS, C_B_VALUE, C_B_PLUS, Y + 114,
          onCard ? CR_ON_B_MINUS : CR_OFF_B_MINUS, onCard ? CR_ON_B_PLUS : CR_OFF_B_PLUS },
    };

    for (uint8_t r = 0; r < 2; r++)
    {
        const int t = rows[r].top;

        b[edIdx(onCard, rows[r].label)].setButton(44, t + 8, 232, t + 38, 0, true, 10, "",
                                                  ALIGN_LEFT, fill, fill, dim);
        b[edIdx(onCard, rows[r].label)].setTextSize(16);
        b[edIdx(onCard, rows[r].label)].setClickable(false);

        b[edIdx(onCard, rows[r].minus)].setButton(240, t, 296, t + 44, (uint16_t)rows[r].crMinus,
                                                  true, 14, "-", ALIGN_CENTER,
                                                  gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[edIdx(onCard, rows[r].minus)].setTextSize(24);

        b[edIdx(onCard, rows[r].value)].setButton(302, t + 4, 390, t + 40, 0, true, 10, "--",
                                                  ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
        b[edIdx(onCard, rows[r].value)].setTextSize(24);
        b[edIdx(onCard, rows[r].value)].setClickable(false);

        b[edIdx(onCard, rows[r].plus)].setButton(396, t, 452, t + 44, (uint16_t)rows[r].crPlus,
                                                 true, 14, "+", ALIGN_CENTER,
                                                 gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[edIdx(onCard, rows[r].plus)].setTextSize(24);
    }
}

uint8_t schedEdit_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;

    b[E_BACK].setButton(20, 58, 112, 98, CR_BACK, true, 14, "Back", ALIGN_CENTER,
                        gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[E_BACK].setTextSize(16);

    b[E_TITLE].setButton(124, 58, 456, 98, 0, true, 10, RELAY_name(s_editLight), ALIGN_LEFT,
                         gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[E_TITLE].setTextSize(16);
    b[E_TITLE].setClickable(false);

    GUI_I.drawCard(24, E_ON_Y,  432, E_CARD_H, 18, fill, shadow, 5);
    GUI_I.drawCard(24, E_OFF_Y, 432, E_CARD_H, 18, fill, shadow, 5);

    buildCard(true);
    buildCard(false);
    applyCard(true);
    applyCard(false);

    return E_BTN_COUNT;
}

// Changing the trigger changes which controls exist, so the card face is
// repainted first — a button that just became invisible would otherwise leave
// its pixels behind, and rebuilding the whole page would flash.
static void refreshCard(bool onCard)
{
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;

    GUI_I.drawCard(24, cardTop(onCard), 432, E_CARD_H, 18, fill, shadow, 5);
    applyCard(onCard);

    for (uint8_t c = 0; c < C_PER_CARD; c++)
        GUI_I.updateButton(edIdx(onCard, c));

    GUI_I.updateScreen();
}

void schedEdit_handler(int userInput)
{
    if (userInput < 0)
        return;

    if (userInput == CR_BACK)
    {
        App* app = GUI_I.getApp();
        if (app) app->newApp(APP_SCHEDULES);
        return;
    }

    const bool onCard = (userInput < CR_OFF_TRIGGER);
    LightSchedule s = SCHED_get(s_editLight);
    const SchedEvent& e = onCard ? s.on : s.off;
    const bool sun = (e.trigger == SCHED_SUNRISE || e.trigger == SCHED_SUNSET);

    switch (userInput)
    {
        case CR_ON_TRIGGER:
        case CR_OFF_TRIGGER:
            SCHED_setTrigger(s_editLight, onCard,
                             (uint8_t)((e.trigger + 1) % SCHED_TRIGGER_COUNT));
            break;

        case CR_ON_A_MINUS: case CR_OFF_A_MINUS:
            if (sun) SCHED_adjustOffset(s_editLight, onCard, -1);
            else     SCHED_adjustHour(s_editLight, onCard, -1);
            break;

        case CR_ON_A_PLUS: case CR_OFF_A_PLUS:
            if (sun) SCHED_adjustOffset(s_editLight, onCard, +1);
            else     SCHED_adjustHour(s_editLight, onCard, +1);
            break;

        case CR_ON_B_MINUS: case CR_OFF_B_MINUS:
            SCHED_adjustMinute(s_editLight, onCard, -1);
            break;

        case CR_ON_B_PLUS: case CR_OFF_B_PLUS:
            SCHED_adjustMinute(s_editLight, onCard, +1);
            break;

        default:
            return;
    }

    refreshCard(onCard);
}
