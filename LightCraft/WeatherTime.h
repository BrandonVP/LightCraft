/*
===========================================================================
Name        : WeatherTime.h
Author      : Brandon Van Pelt
Description : WiFi + NTP time + OpenWeatherMap for LightCraft (ESP32-S3).

              Connects to WiFi (non-blocking), sets the clock over NTP, and
              periodically fetches current weather. The Home tab reads the
              results through the getters below. Credentials live in secrets.h.
===========================================================================
*/
#ifndef WEATHERTIME_H
#define WEATHERTIME_H

#include <stdint.h>

struct WeatherData
{
    char    city[24];
    int16_t temperature;  // deg F
    int16_t realFeel;     // deg F
    int16_t tempHigh;     // deg F
    int16_t tempLow;      // deg F
    uint8_t humidity;     // %
    char    condition[16];// OWM "main", e.g. "Clouds"
    char    iconCode[4];  // OWM icon code, e.g. "10d" (see WeatherIcons.h)
    bool    valid;        // true once an OpenWeatherMap fetch has succeeded

    // Room reading from the ESP8266 weather station (LAN).
    int16_t  roomTempF;    // deg F
    uint8_t  roomHumidity; // %
    bool     roomValid;    // true once the /room endpoint has answered
    uint32_t roomStampMs;  // millis() of that answer — lets a consumer reject a
                           // stale reading (only meaningful while roomValid)
};

// --- Multi-day forecast ----------------------------------------------------
// Built from OpenWeatherMap's free 5 day / 3 hour endpoint: the 3-hourly slots
// are folded into local calendar days. Day 0 is today, so its high/low cover
// only the hours still to come.
#define FORECAST_DAYS 5

struct ForecastDay
{
    int16_t tempHigh;     // deg F
    int16_t tempLow;      // deg F
    uint8_t wday;         // 0 = Sunday .. 6 = Saturday
    char    iconCode[4];  // OWM icon code of the slot nearest midday
    bool    valid;
};

// Start the network task (WiFi + NTP + weather) on core 0. Call once in setup().
// Returns immediately; all blocking work happens on the task, not the UI loop.
void weather_begin();

// Re-associate using whatever WiFiConfig now holds. Call after saving new
// credentials; the reconnect happens on the network task, not the caller.
void weather_reconnect();

bool weather_isConnected();          // WiFi associated
bool weather_timeValid();            // NTP has produced a real wall-clock time
WeatherData weather_get();           // thread-safe snapshot of the latest reading
uint32_t weather_updateCount();      // bumps on each successful weather fetch

// Thread-safe snapshot of the forecast. Entries with valid == false are unfilled.
void     forecast_get(ForecastDay out[FORECAST_DAYS]);
uint32_t forecast_updateCount();     // bumps on each successful forecast fetch
bool     forecast_isValid();         // true once a forecast has been parsed
void     forecast_request();         // ask the task to refresh at its next pass

#endif // WEATHERTIME_H
