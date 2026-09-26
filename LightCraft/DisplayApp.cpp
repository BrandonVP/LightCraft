/*
===========================================================================
Name        : DisplayApp.cpp
Author      : Brandon Van Pelt
Description : Settings > Display (see DisplayApp.h).
===========================================================================
*/
#include "DisplayApp.h"
#include "Backlight.h"

// --- Rows ------------------------------------------------------------------
enum { ROW_BRIGHT = 0, ROW_DIM, ROW_AFTER, ROW_COUNT };

static const char* const ROW_LABEL[ROW_COUNT] = { "Brightness", "Dim to", "Dim after" };

// --- Button layout ---------------------------------------------------------
enum { COL_LABEL = 0, COL_MINUS, COL_VALUE, COL_PLUS, COL_PER_ROW };

enum {
    IDX_HEADING = 0,
    IDX_ROW0,
    DISPLAY_BTN_COUNT = IDX_ROW0 + (int)ROW_COUNT * (int)COL_PER_ROW
};

static inline uint8_t colIdx(uint8_t row, uint8_t column)
{
    return (uint8_t)(IDX_ROW0 + row * COL_PER_ROW + column);
}

static const int CR_MINUS_BASE = 10;   // 10..12
static const int CR_PLUS_BASE  = 20;   // 20..22

// --- Geometry --------------------------------------------------------------
static const int ROW_Y0     = 104;
static const int ROW_PITCH  = 100;
static const int ROW_HEIGHT = 90;

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

// --- Values ----------------------------------------------------------------
static void setValueLabel(uint8_t row)
{
    UserInterfaceClass& v = GUI_I.appButtons()[colIdx(row, COL_VALUE)];

    switch (row)
    {
        case ROW_BRIGHT: v.setTextFormat("%u%%", BACKLIGHT_activePct()); break;
        case ROW_DIM:    v.setTextFormat("%u%%", BACKLIGHT_idlePct());   break;
        default:         v.setText(BACKLIGHT_timeoutName(BACKLIGHT_timeoutIndex())); break;
    }
}

static void step(uint8_t row, int dir)
{
    switch (row)
    {
        case ROW_BRIGHT:
            BACKLIGHT_setActivePct((uint8_t)(BACKLIGHT_activePct() + dir * BL_STEP_PCT));
            break;

        case ROW_DIM:
        {
            // Unsigned, so guard the bottom before subtracting.
            const uint8_t cur = BACKLIGHT_idlePct();
            if (dir < 0 && cur < BL_STEP_PCT) BACKLIGHT_setIdlePct(0);
            else                              BACKLIGHT_setIdlePct((uint8_t)(cur + dir * BL_STEP_PCT));
            break;
        }

        default:
        {
            const uint8_t cur = BACKLIGHT_timeoutIndex();
            if (dir < 0 && cur == 0) break;
            if (dir > 0 && cur + 1 >= BACKLIGHT_timeoutCount()) break;
            BACKLIGHT_setTimeoutIndex((uint8_t)(cur + dir));
            break;
        }
    }
}

// --- Page ------------------------------------------------------------------
uint8_t display_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;

    b[IDX_HEADING].setButton(24, 56, 456, 92, 0, true, 10, "Display", ALIGN_LEFT,
                             gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_HEADING].setTextSize(16);
    b[IDX_HEADING].setClickable(false);

    for (uint8_t i = 0; i < ROW_COUNT; i++)
    {
        const int y = ROW_Y0 + i * ROW_PITCH;

        GUI_I.drawCard(24, y, 432, ROW_HEIGHT, 16, fill, shadow, 5);

        b[colIdx(i, COL_LABEL)].setButton(44, y + 30, 232, y + 60, 0, true, 10,
                                          ROW_LABEL[i], ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
        b[colIdx(i, COL_LABEL)].setTextSize(16);
        b[colIdx(i, COL_LABEL)].setClickable(false);

        b[colIdx(i, COL_MINUS)].setButton(240, y + 18, 296, y + 72,
                                          (uint16_t)(CR_MINUS_BASE + i), true, 14, "-", ALIGN_CENTER,
                                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[colIdx(i, COL_MINUS)].setTextSize(24);

        b[colIdx(i, COL_VALUE)].setButton(302, y + 22, 390, y + 68, 0, true, 10, "--", ALIGN_CENTER,
                                          fill, fill, gfxTheme.btnTextColor);
        b[colIdx(i, COL_VALUE)].setTextSize(24);
        b[colIdx(i, COL_VALUE)].setClickable(false);

        b[colIdx(i, COL_PLUS)].setButton(396, y + 18, 452, y + 72,
                                         (uint16_t)(CR_PLUS_BASE + i), true, 14, "+", ALIGN_CENTER,
                                         gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[colIdx(i, COL_PLUS)].setTextSize(24);

        setValueLabel(i);
    }

    return DISPLAY_BTN_COUNT;
}

void display_handler(int userInput)
{
    int row = -1, dir = 0;

    if (userInput >= CR_MINUS_BASE && userInput < CR_MINUS_BASE + ROW_COUNT)
    {
        row = userInput - CR_MINUS_BASE;
        dir = -1;
    }
    else if (userInput >= CR_PLUS_BASE && userInput < CR_PLUS_BASE + ROW_COUNT)
    {
        row = userInput - CR_PLUS_BASE;
        dir = +1;
    }
    else
    {
        return;
    }

    step((uint8_t)row, dir);

    // Raising brightness can pull the idle level down with it, so repaint both.
    setValueLabel(ROW_BRIGHT);
    setValueLabel(ROW_DIM);
    setValueLabel(ROW_AFTER);
    for (uint8_t i = 0; i < ROW_COUNT; i++)
        GUI_I.updateButton(colIdx(i, COL_VALUE));
    GUI_I.updateScreen();
}
