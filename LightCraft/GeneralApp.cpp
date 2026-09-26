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

// Label and hint share the left column; keep both within TEXT_CHARS (below) or
// they run under the toggle. They are drawn truncated rather than clipped, so
// an over-long one is visibly cut instead of spilling across the row.
static const ToggleRow ROWS[] = {
    { "Mini-split card", "on the Control tab", GSET_minisplitCard, GSET_setMinisplitCard },
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

// Left column holding the label and hint, and how much text fits in it. The
// built-in font is 6 px per character at 1x and text size 16 renders at 2x, so
// 12 px; ALIGN_LEFT insets the first character by 5.
static const int TEXT_X1 = 44, TEXT_X2 = 320;
static const int TEXT_CHARS = (TEXT_X2 - TEXT_X1 - 5) / 12;   // 22

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

        b[rowIdx(i, COL_LABEL)].setButton(TEXT_X1, y + 16, TEXT_X2, y + 46, 0, true, 10,
                                          "", ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
        b[rowIdx(i, COL_LABEL)].setTextFormat("%.*s", TEXT_CHARS, ROWS[i].label);
        b[rowIdx(i, COL_LABEL)].setTextSize(16);
        b[rowIdx(i, COL_LABEL)].setClickable(false);

        b[rowIdx(i, COL_HINT)].setButton(TEXT_X1, y + 48, TEXT_X2, y + 76, 0, true, 10,
                                         "", ALIGN_LEFT, fill, fill, dim);
        b[rowIdx(i, COL_HINT)].setTextFormat("%.*s", TEXT_CHARS, ROWS[i].hint);
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
