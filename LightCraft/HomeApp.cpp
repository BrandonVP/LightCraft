/*
===========================================================================
Name        : HomeApp.cpp
Author      : Brandon Van Pelt
Description : Home tab (see HomeApp.h). Two floating cards: a clock card on
              top (time + date) and a weather card below (drawn condition icon,
              current temperature, and the room reading from the local station).
              Tapping the weather card opens the 5-day forecast.
              Network/time/weather live in WeatherTime.*.
===========================================================================
*/
#include "HomeApp.h"
#include "WeatherTime.h"
#include "WeatherIcons.h"
#include <App.h>
#include <time.h>
#include <sys/time.h>

static const char* const MONTHS[12] =
    { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
static const char* const DOW[7] =
    { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

// Button indices on the shared app-button array. WCARD_IDX is the weather
// card's own face: a clickable button the size of the card, drawn before the
// labels that sit on it.
enum { TIME_IDX = 0, DATE_IDX, WCARD_IDX, CITY_IDX, CHEV_IDX, TEMP_IDX, COND_IDX,
       DIVIDER_IDX, FEELS_IDX, HILO_IDX, ROOM_IDX, HOME_BTN_COUNT };

static const int CR_WEATHER_CARD = 1;   // click return of the weather card

// Weather card geometry (also used by the directly-drawn icon + chevron).
static const int WCARD_X = 24,  WCARD_Y = 206;
static const int WCARD_W = 432, WCARD_H = 246;
static const int ICON_CX = 104, ICON_CY = 300, ICON_SIZE = 92;

static uint16_t cardFill(void) { return gfxCardFill(gfxTheme.background); }

// Icon currently painted on the card, so it is only redrawn when it changes.
static WeatherIcon s_shownIcon = WICON_NONE;
static bool        s_iconDrawn = false;

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
    WeatherData w = weather_get();
    if (w.valid)
    {
        // setTextFormat, not setText: setText only stores the pointer, and w is
        // a local snapshot that is gone by the time the page draws.
        b[CITY_IDX].setTextFormat("%s", w.city);
        b[TEMP_IDX].setTextFormat("%d\xF8" "F", w.temperature);
        b[COND_IDX].setTextFormat("%s", w.condition);
        b[FEELS_IDX].setTextFormat("Feels %d\xF8   Humidity %u%%", w.realFeel, w.humidity);
        b[HILO_IDX].setTextFormat("High %d\xF8    Low %d\xF8", w.tempHigh, w.tempLow);
    }
    else
    {
        b[CITY_IDX].setText(weather_isConnected() ? "Loading weather..." : "Connecting to WiFi...");
        b[TEMP_IDX].setText("--\xF8" "F");
        b[COND_IDX].setText("");
        b[FEELS_IDX].setText("");
        b[HILO_IDX].setText("");
    }

    // Room reading from the weather station (independent of the OWM fetch).
    if (w.roomValid)
        b[ROOM_IDX].setTextFormat("Room %d\xF8" "F    %u%%", w.roomTempF, w.roomHumidity);
    else
        b[ROOM_IDX].setText("Room --");
}

// The condition icon is the one part of the card that is not a button, so it
// has to be laid back on after each page render: the card face (WCARD_IDX)
// paints over the whole card during the button pass.
static void drawWeatherIcon(bool force)
{
    WeatherData w = weather_get();
    WeatherIcon icon = w.valid ? wicon_fromOwm(w.iconCode) : WICON_NONE;

    if (!force && s_iconDrawn && icon == s_shownIcon)
        return;

    wicon_draw(ICON_CX, ICON_CY, ICON_SIZE, icon, cardFill());
    s_shownIcon = icon;
    s_iconDrawn = true;
}

uint8_t home_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    // Body is cleared to the solid background by the framework; depth comes from
    // the raised cards + drop shadows (a subtle gradient bands on this panel).
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;   // pure black drop shadow

    // --- Card 1: time + date ------------------------------------------------
    GUI_I.drawCard(24, 62, 432, 132, 20, fill, shadow, 6);

    b[TIME_IDX].setButton(44, 76, 436, 146, 0, true, 12, "--:--:--", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[TIME_IDX].setTextSize(40);  b[TIME_IDX].setClickable(false);

    b[DATE_IDX].setButton(44, 150, 436, 182, 0, true, 12, "--- --- --", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[DATE_IDX].setTextSize(16);  b[DATE_IDX].setClickable(false);

    // --- Card 2: weather + room (tappable) ----------------------------------
    // The shadow comes from drawCard; the face is the button itself, so the
    // whole card highlights and responds when tapped.
    GUI_I.drawCard(WCARD_X, WCARD_Y, WCARD_W, WCARD_H, 20, fill, shadow, 6);

    b[WCARD_IDX].setButton(WCARD_X, WCARD_Y, WCARD_X + WCARD_W, WCARD_Y + WCARD_H,
                           CR_WEATHER_CARD, true, 20, "", ALIGN_CENTER,
                           fill, fill, gfxTheme.btnBorder, fill);

    b[CITY_IDX].setButton(44, 212, 380, 240, 0, true, 12, "Connecting to WiFi...", ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
    b[CITY_IDX].setTextSize(16);  b[CITY_IDX].setClickable(false);

    // Chevron marking the card as tappable.
    b[CHEV_IDX].setButton(400, 208, 444, 248, 0, true, 12, ">", ALIGN_CENTER, fill, fill, gfxShade(gfxTheme.btnTextColor, -25));
    b[CHEV_IDX].setTextSize(24);  b[CHEV_IDX].setClickable(false);

    b[TEMP_IDX].setButton(164, 244, 440, 308, 0, true, 12, "--\xF8" "F", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[TEMP_IDX].setTextSize(40);  b[TEMP_IDX].setClickable(false);

    b[COND_IDX].setButton(164, 314, 440, 340, 0, true, 12, "", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[COND_IDX].setTextSize(16);  b[COND_IDX].setClickable(false);

    // Hairline between the outdoor block and the room reading: a 1 px-tall
    // square button, so it goes through the normal render like everything else.
    {
        const uint16_t rule = gfxShade(fill, -18);
        b[DIVIDER_IDX].setButton(52, 404, 428, 405, 0, false, 0, "", ALIGN_CENTER, rule, rule, rule);
        b[DIVIDER_IDX].setClickable(false);
    }

    b[FEELS_IDX].setButton(44, 350, 436, 376, 0, true, 12, "", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[FEELS_IDX].setTextSize(16); b[FEELS_IDX].setClickable(false);

    b[HILO_IDX].setButton(44, 378, 436, 402, 0, true, 12, "", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[HILO_IDX].setTextSize(16);  b[HILO_IDX].setClickable(false);

    b[ROOM_IDX].setButton(44, 412, 436, 444, 0, true, 12, "Room --", ALIGN_CENTER, fill, fill, gfxTheme.btnTextColor);
    b[ROOM_IDX].setTextSize(16);  b[ROOM_IDX].setClickable(false);

    // Populate with the current time + saved weather so a (re)entry to Home
    // shows real data immediately, without waiting for home_tick.
    setTimeLabels();
    setWeatherLabels();

    // The card face is about to be repainted over the icon; home_tick puts it
    // back once this render finishes.
    s_iconDrawn = false;

    return HOME_BTN_COUNT;
}

void home_handler(int userInput)
{
    if (userInput != CR_WEATHER_CARD)
        return;

    forecast_request();             // refresh in the background while it opens
    App* app = GUI_I.getApp();
    if (app) app->newApp(APP_FORECAST);
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
    static int      lastMday    = -1;
    static uint32_t lastClockMs = 0;
    static uint32_t lastWeather = 0xFFFFFFFFu;
    static bool     lastConn    = false;

    // Only touch the shared button array while the Home tab owns it AND its
    // render has settled — drawing into it mid-transition (while the framework
    // is (re)building the page) races with the app render. (The initial fill
    // happens in home_createBtns, so entry already shows current data.)
    if (!app || app->getActiveApp() != APP_HOME || app->renderState != App::APP_STATE_DONE)
    {
        lastSec = -1;
        return;
    }

    // First pass after a render: the card face has just been painted, so the
    // icon needs laying back on top.
    if (!s_iconDrawn)
    {
        drawWeatherIcon(true);
        GUI_I.updateScreen();
    }

    // Clock: redraw once per second. The loop runs thousands of times a second,
    // so the wall clock is only consulted every 100 ms — localtime() is not
    // free, and the seconds digit cannot move faster than this anyway.
    if (millis() - lastClockMs >= 100)
    {
        lastClockMs = millis();

        time_t    now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);

        if (t.tm_sec != lastSec)
        {
            lastSec = t.tm_sec;
            setTimeLabels();
            GUI_I.updateButton(TIME_IDX);

            // The date only moves once a day; repainting it every second was
            // pure framebuffer traffic.
            if (t.tm_mday != lastMday)
            {
                lastMday = t.tm_mday;
                GUI_I.updateButton(DATE_IDX);
            }
        }
    }

    // Weather: redraw only when a new reading arrives or the link state changes.
    uint32_t wc   = weather_updateCount();
    bool     conn = weather_isConnected();
    if (wc != lastWeather || conn != lastConn)
    {
        lastWeather = wc;
        lastConn = conn;
        setWeatherLabels();
        for (int i = CITY_IDX; i <= ROOM_IDX; i++)
            GUI_I.updateButton(i);
        drawWeatherIcon(false);       // only repaints if the condition changed
    }

    GUI_I.updateScreen();
}
