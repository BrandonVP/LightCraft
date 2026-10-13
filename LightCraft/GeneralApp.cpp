/*
===========================================================================
Name        : GeneralApp.cpp
Author      : Brandon Van Pelt
Description : Settings > General (see GeneralApp.h).
===========================================================================
*/
#include "GeneralApp.h"
#include "GeneralSettings.h"
#include "RelayControl.h"
#include <apps/KeyboardApp.h>
#include <App.h>

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

enum {
    IDX_HEADING = 0,
    IDX_ROW0,
    IDX_NAMES_HEAD = IDX_ROW0 + ROW_COUNT * COL_PER_ROW,
    IDX_NAME0,                                     // 2 slots per switch
    GENERAL_BTN_COUNT = IDX_NAME0 + (int)LIGHT_COUNT * 2
};

static inline uint8_t rowIdx(uint8_t row, uint8_t column)
{
    return (uint8_t)(IDX_ROW0 + row * COL_PER_ROW + column);
}

// Per switch: a fixed "Switch N" caption and the editable name beside it.
static inline uint8_t nameCapIdx(uint8_t i)  { return (uint8_t)(IDX_NAME0 + 2 * i + 0); }
static inline uint8_t nameBtnIdx(uint8_t i)  { return (uint8_t)(IDX_NAME0 + 2 * i + 1); }

static const int CR_TOGGLE_BASE = 1;
static const int CR_NAME_BASE   = 10;   // 10..12, one per switch

// Which switch the keyboard was opened for.
static uint8_t s_editing = 0;

// --- Geometry --------------------------------------------------------------
static const int ROW_Y0     = 104;
static const int ROW_PITCH  = 100;
static const int ROW_HEIGHT = 90;

// Left column holding the label and hint, and how much text fits in it. The
// built-in font is 6 px per character at 1x and text size 16 renders at 2x, so
// 12 px; ALIGN_LEFT insets the first character by 5.
static const int TEXT_X1 = 44, TEXT_X2 = 320;
static const int TEXT_CHARS = (TEXT_X2 - TEXT_X1 - 5) / 12;   // 22

// --- Switch names ----------------------------------------------------------
// Sits below the toggle rows and takes whatever is left of the screen, so
// adding a toggle above pushes it down rather than overlapping it.
static const int NAMES_Y     = ROW_Y0 + ROW_COUNT * ROW_PITCH + 6;   // 210
static const int NAMES_H     = 480 - NAMES_Y - 12;                   // 258
static const int NAMES_ROW_Y = NAMES_Y + 48;
static const int NAMES_PITCH = 66;
static const int NAMES_ROW_H = 54;

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

    // --- Switch names ------------------------------------------------------
    GUI_I.drawCard(24, NAMES_Y, 432, NAMES_H, 16, fill, shadow, 5);

    b[IDX_NAMES_HEAD].setButton(TEXT_X1, NAMES_Y + 10, 436, NAMES_Y + 40, 0, true, 10,
                                "Switch names", ALIGN_LEFT, fill, fill, dim);
    b[IDX_NAMES_HEAD].setTextSize(16);
    b[IDX_NAMES_HEAD].setClickable(false);

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        const int y = NAMES_ROW_Y + i * NAMES_PITCH;

        // The number is positional and never changes, so it stays readable as
        // "which physical switch is this" however the name is edited.
        b[nameCapIdx(i)].setButton(TEXT_X1, y + 12, 160, y + 42, 0, true, 10,
                                   "", ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
        b[nameCapIdx(i)].setTextFormat("Switch %u", (unsigned)(i + 1));
        b[nameCapIdx(i)].setTextSize(16);
        b[nameCapIdx(i)].setClickable(false);

        b[nameBtnIdx(i)].setButton(170, y, 436, y + NAMES_ROW_H,
                                   (uint16_t)(CR_NAME_BASE + i), true, 14,
                                   "", ALIGN_CENTER,
                                   gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[nameBtnIdx(i)].setText(RELAY_name(i));
        b[nameBtnIdx(i)].setTextSize(16);
    }

    return (uint8_t)GENERAL_BTN_COUNT;
}

// --- Keyboard hand-off -----------------------------------------------------
static void onNameEntered(const char* text, bool accepted)
{
    if (!accepted)
        return;

    // RELAY_setName trims, caps the length and restores the default on empty,
    // so there is nothing to validate here. Returning to this page re-runs
    // general_createBtns, which picks the new name straight back up.
    RELAY_setName(s_editing, text);
}

void general_handler(int userInput)
{
    // --- Rename a switch ---------------------------------------------------
    if (userInput >= CR_NAME_BASE && userInput < CR_NAME_BASE + (int)LIGHT_COUNT)
    {
        s_editing = (uint8_t)(userInput - CR_NAME_BASE);

        char title[32];
        snprintf(title, sizeof(title), "Name for switch %u", (unsigned)(s_editing + 1));

        KeyboardApp_open(title, RELAY_name(s_editing), RELAY_NAME_MAX - 1,
                         false /* not masked */, APP_GENERAL, onNameEntered);

        App* app = GUI_I.getApp();
        if (app) app->newApp(APP_KEYBOARD);
        return;
    }

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
