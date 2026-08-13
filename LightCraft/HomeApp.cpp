/*
===========================================================================
Name        : HomeApp.cpp
Author      : Brandon Van Pelt
Description : Home tab placeholder (see HomeApp.h). Static labels for time,
              date and weather until the NTP clock + weather API are wired in
              (that reuses API keys from another project).
===========================================================================
*/
#include "HomeApp.h"

uint8_t home_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    // Time (big, centered).
    b[0].setButton(40, 110, 440, 200, 0, true, 12, "--:--", ALIGN_CENTER,
                   gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[0].setTextSize(40);
    b[0].setClickable(false);

    // Date.
    b[1].setButton(40, 210, 440, 270, 0, true, 12, "--- --- --", ALIGN_CENTER,
                   gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[1].setTextSize(20);
    b[1].setClickable(false);

    // Weather (future feature).
    b[2].setButton(40, 300, 440, 360, 0, true, 12, "Weather - coming soon", ALIGN_CENTER,
                   gfxTheme.background, gfxTheme.background, gfxTheme.btnTextColor);
    b[2].setTextSize(16);
    b[2].setClickable(false);

    return 3;
}

void home_handler(int userInput)
{
    (void)userInput; // static page for now
}
