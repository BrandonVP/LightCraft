/*
===========================================================================
Name        : HomeApp.cpp
Author      : Brandon Van Pelt
Description : Home tab (see HomeApp.h). Live clock + date on a floating card;
              weather is a future feature. The clock is seeded from the build
              timestamp so it runs before NTP is wired in.
===========================================================================
*/
#include "HomeApp.h"
#include <App.h>
#include <time.h>
#include <sys/time.h>

static const char* const MONTHS[12] =
    { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
static const char* const DOW[7] =
    { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

// Button indices on the shared app-button array.
static const uint8_t TIME_IDX = 0;
static const uint8_t DATE_IDX = 1;

uint8_t home_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    // Body is cleared to the solid background by the framework; depth comes from
    // the raised card + drop shadow (a subtle gradient bands on this panel).
    const uint16_t cardFill   = gfxShade(gfxTheme.background, 15);
    const uint16_t cardShadow = 0x0000;   // pure black drop shadow

    // Floating panel behind the clock / date / weather.
    GUI_I.drawCard(24, 90, 432, 300, 20, cardFill, cardShadow, 6);

    // Time (big, centered) — labels blend onto the card.
    b[TIME_IDX].setButton(40, 110, 440, 200, 0, true, 12, "--:--:--", ALIGN_CENTER,
                          cardFill, cardFill, gfxTheme.btnTextColor);
    b[TIME_IDX].setTextSize(40);
    b[TIME_IDX].setClickable(false);

    // Date.
    b[DATE_IDX].setButton(40, 210, 440, 270, 0, true, 12, "--- --- --", ALIGN_CENTER,
                          cardFill, cardFill, gfxTheme.btnTextColor);
    b[DATE_IDX].setTextSize(20);
    b[DATE_IDX].setClickable(false);

    // Weather (future feature).
    b[2].setButton(40, 300, 440, 360, 0, true, 12, "Weather - coming soon", ALIGN_CENTER,
                   cardFill, cardFill, gfxTheme.btnTextColor);
    b[2].setTextSize(16);
    b[2].setClickable(false);

    return 3;
}

void home_handler(int userInput)
{
    (void)userInput; // static page for now
}

void home_seedClockFromBuild(void)
{
    char mon[4] = { 0 };
    int dd = 1, yyyy = 2026, hh = 0, mm = 0, ss = 0;
    sscanf(__DATE__, "%3s %d %d", mon, &dd, &yyyy);   // "Mmm dd yyyy"
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);      // "hh:mm:ss"

    int month = 0;
    for (int i = 0; i < 12; i++)
        if (strncmp(mon, MONTHS[i], 3) == 0) { month = i; break; }

    struct tm tmv = {};
    tmv.tm_year = yyyy - 1900;
    tmv.tm_mon  = month;
    tmv.tm_mday = dd;
    tmv.tm_hour = hh;
    tmv.tm_min  = mm;
    tmv.tm_sec  = ss;
    tmv.tm_isdst = 0;

    time_t t = mktime(&tmv);
    struct timeval tv = { t, 0 };
    settimeofday(&tv, nullptr);
}

void home_tick(void)
{
    App* app = GUI_I.getApp();

    // Only touch the shared button array while the Home tab owns it.
    static int lastSec = -1;
    if (!app || app->getActiveMenu() != MENU_home)
    {
        lastSec = -1;   // force a refresh next time Home is shown
        return;
    }

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    if (t->tm_sec == lastSec)
        return;
    lastSec = t->tm_sec;

    GUI_I.appButtons()[TIME_IDX].setTextFormat("%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    GUI_I.updateButton(TIME_IDX);
    GUI_I.appButtons()[DATE_IDX].setTextFormat("%s  %s %d  %d",
        DOW[t->tm_wday], MONTHS[t->tm_mon], t->tm_mday, t->tm_year + 1900);
    GUI_I.updateButton(DATE_IDX);
    GUI_I.updateScreen();
}
