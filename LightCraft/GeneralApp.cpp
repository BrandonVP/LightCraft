/*
===========================================================================
Name        : GeneralApp.cpp
Author      : Brandon Van Pelt
Description : Settings > General (see GeneralApp.h).
===========================================================================
*/
#include "GeneralApp.h"
#include "GeneralSettings.h"

// --- The settings this page lists ------------------------------------------
struct ToggleRow {
    const char* label;
    const char* hint;
    bool (*get)(void);
    void (*set)(bool);
};

static const ToggleRow ROWS[] = {
    { "Mini-split card", "shown on the Control tab", GSET_minisplitCard, GSET_setMinisplitCard },
};

static const uint8_t ROW_COUNT = (uint8_t)(sizeof(ROWS) / sizeof(ROWS[0]));

// --- Button layout ---------------------------------------------------------
enum { COL_LABEL = 0, COL_HINT, COL_TOGGLE, COL_PER_ROW };

enum { IDX_HEADING = 0, IDX_ROW0 };

static inline uint8_t rowIdx(uint8_t row, uint8_t column)
{
    return (uint8_t)(IDX_ROW0 + row * COL_PER_ROW + column);
}

static const int CR_TOGGLE_BASE = 1;

// --- Geometry --------------------------------------------------------------
static const int ROW_Y0     = 104;
static const int ROW_PITCH  = 100;
static const int ROW_HEIGHT = 90;

static const uint16_t COL_ON = 0x07E0;   // green, as everywhere else on/off shows

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

static void styleToggle(uint8_t row)
{
    UserInterfaceClass& t = GUI_I.appButtons()[rowIdx(row, COL_TOGGLE)];
    const bool on = ROWS[row].get();

    t.setText(on ? "ON" : "OFF");
    t.setBgColor(on ? COL_ON : gfxTheme.btnColor);
    t.setBorderColor(on ? COL_ON : gfxTheme.btnBorder);
    t.setTextColor(on ? 0x0000 : gfxTheme.btnText);
}

uint8_t general_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    b[IDX_HEADING].setButton(24, 56, 456, 92, 0, true, 10, "General", ALIGN_LEFT,
                             gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[IDX_HEADING].setTextSize(16);
    b[IDX_HEADING].setClickable(false);

    for (uint8_t i = 0; i < ROW_COUNT; i++)
    {
        const int y = ROW_Y0 + i * ROW_PITCH;

        GUI_I.drawCard(24, y, 432, ROW_HEIGHT, 16, fill, shadow, 5);

        b[rowIdx(i, COL_LABEL)].setButton(44, y + 16, 320, y + 46, 0, true, 10,
                                          ROWS[i].label, ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
        b[rowIdx(i, COL_LABEL)].setTextSize(16);
        b[rowIdx(i, COL_LABEL)].setClickable(false);

        b[rowIdx(i, COL_HINT)].setButton(44, y + 48, 320, y + 76, 0, true, 10,
                                         ROWS[i].hint, ALIGN_LEFT, fill, fill, dim);
        b[rowIdx(i, COL_HINT)].setTextSize(16);
        b[rowIdx(i, COL_HINT)].setClickable(false);

        b[rowIdx(i, COL_TOGGLE)].setButton(336, y + 20, 436, y + 70,
                                           (uint16_t)(CR_TOGGLE_BASE + i), true, 14, "OFF", ALIGN_CENTER,
                                           gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[rowIdx(i, COL_TOGGLE)].setTextSize(16);

        styleToggle(i);
    }

    return (uint8_t)(IDX_ROW0 + ROW_COUNT * COL_PER_ROW);
}

void general_handler(int userInput)
{
    if (userInput < CR_TOGGLE_BASE || userInput >= CR_TOGGLE_BASE + ROW_COUNT)
        return;

    const uint8_t row = (uint8_t)(userInput - CR_TOGGLE_BASE);

    // Saved immediately: a single toggle has nothing to batch, and a settings
    // page the user leaves straight away should not lose the change.
    ROWS[row].set(!ROWS[row].get());

    styleToggle(row);
    GUI_I.updateButton(rowIdx(row, COL_TOGGLE));
    GUI_I.updateScreen();
}
