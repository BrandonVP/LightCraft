/*
===========================================================================
Name        : HomeApp.cpp
Author      : Brandon Van Pelt
Description : Home tab (see HomeApp.h). Live clock (NTP, seeded from build time
              until it syncs) + current weather from OpenWeatherMap, on a
              floating card. Network/time/weather live in WeatherTime.*.
===========================================================================
*/
#include "HomeApp.h"
#include "WeatherTime.h"
#include <App.h>
#include <time.h>
#include <sys/time.h>

static const char* const MONTHS[12] =
    { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
static const char* const DOW[7] =
    { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

// Button indices on the shared app-button array.
enum { TIME_IDX = 0, DATE_IDX, TEMP_IDX, FEELS_IDX, HILO_IDX, CITY_IDX, HOME_BTN_COUNT };

// Set the clock label texts from the current time (no drawing).
static void setTimeLabels(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    b[TIME_IDX].setTextFormat("%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    b[DATE_IDX].setTextFormat("%s  %s %d  %d",
        DOW[t->tm_wday], MONTHS[t->tm_mon], t->tm_mday, t->tm_year + 1900);
}

// Set the weather label texts from the latest reading, or a status line (no drawing).
static void setWeatherLabels(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const WeatherData& w = weather_get();
    if (w.valid)
    {
        b[TEMP_IDX].setTextFormat("%d\xF8" "F", w.temperature);
        b[FEELS_IDX].setTextFormat("Feels %d\xF8   Humidity %u%%", w.realFeel, w.humidity);
        b[HILO_IDX].setTextFormat("High %d\xF8    Low %d\xF8", w.tempHigh, w.tempLow);
        b[CITY_IDX].setText(w.city);
    }
    else
    {
        b[TEMP_IDX].setText("--\xF8" "F");
        b[FEELS_IDX].setText("");
        b[HILO_IDX].setText("");
        b[CITY_IDX].setText(weather_isConnected() ? "Loading weather..." : "Connecting to WiFi...");
    }
}

uint8_t home_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    // Body is cleared to the solid background by the framework; depth comes from
    // the raised card + drop shadow (a subtle gradient bands on this panel).
    const uint16_t cardFill   = gfxShade(gfxTheme.background, 15);
    const uint16_t cardShadow = 0x0000;   // pure black drop shadow

    GUI_I.drawCard(24, 84, 432, 312, 20, cardFill, cardShadow, 6);

    // All labels blend onto the card (bg = cardFill).
    b[TIME_IDX].setButton(40, 92, 440, 152, 0, true, 12, "--:--:--", ALIGN_CENTER, cardFill, cardFill, gfxTheme.btnTextColor);
    b[TIME_IDX].setTextSize(34);  b[TIME_IDX].setClickable(false);

    b[DATE_IDX].setButton(40, 156, 440, 188, 0, true, 12, "--- --- --", ALIGN_CENTER, cardFill, cardFill, gfxTheme.btnTextColor);
    b[DATE_IDX].setTextSize(16);  b[DATE_IDX].setClickable(false);

    b[TEMP_IDX].setButton(40, 206, 440, 268, 0, true, 12, "--\xF8" "F", ALIGN_CENTER, cardFill, cardFill, gfxTheme.btnTextColor);
    b[TEMP_IDX].setTextSize(40);  b[TEMP_IDX].setClickable(false);

    b[FEELS_IDX].setButton(40, 276, 440, 303, 0, true, 12, "", ALIGN_CENTER, cardFill, cardFill, gfxTheme.btnTextColor);
    b[FEELS_IDX].setTextSize(16); b[FEELS_IDX].setClickable(false);

    b[HILO_IDX].setButton(40, 307, 440, 334, 0, true, 12, "", ALIGN_CENTER, cardFill, cardFill, gfxTheme.btnTextColor);
    b[HILO_IDX].setTextSize(16);  b[HILO_IDX].setClickable(false);

    b[CITY_IDX].setButton(40, 346, 440, 382, 0, true, 12, "Connecting to WiFi...", ALIGN_CENTER, cardFill, cardFill, gfxTheme.btnTextColor);
    b[CITY_IDX].setTextSize(16);  b[CITY_IDX].setClickable(false);

    // Populate with the current time + saved weather so a (re)entry to Home
    // shows real data immediately, without waiting for home_tick.
    setTimeLabels();
    setWeatherLabels();

    return HOME_BTN_COUNT;
}

void home_handler(int userInput)
{
    (void)userInput; // static page
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

    static int      lastSec     = -1;
    static uint32_t lastWeather = 0xFFFFFFFFu;
    static bool     lastConn    = false;

    // Only touch the shared button array while the Home tab owns it AND its
    // render has settled — drawing into it mid-transition (while the framework
    // is (re)building the page) races with the app render. (The initial fill
    // happens in home_createBtns, so entry already shows current data.)
    if (!app || app->getActiveMenu() != MENU_home || app->renderState != App::APP_STATE_DONE)
    {
        lastSec = -1;
        return;
    }

    // Clock: redraw once per second.
    time_t now = time(nullptr);
    int sec = localtime(&now)->tm_sec;
    if (sec != lastSec)
    {
        lastSec = sec;
        setTimeLabels();
        GUI_I.updateButton(TIME_IDX);
        GUI_I.updateButton(DATE_IDX);
    }

    // Weather: redraw only when a new reading arrives or the link state changes.
    uint32_t wc   = weather_updateCount();
    bool     conn = weather_isConnected();
    if (wc != lastWeather || conn != lastConn)
    {
        lastWeather = wc;
        lastConn = conn;
        setWeatherLabels();
        for (int i = TEMP_IDX; i <= CITY_IDX; i++)
            GUI_I.updateButton(i);
    }

    GUI_I.updateScreen();
}
