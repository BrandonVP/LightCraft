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
    bool    valid;        // true once a fetch has succeeded
};

// Start WiFi (returns immediately) and set the timezone. Call once in setup().
void weather_begin();

// Call every loop(): polls the WiFi link, starts NTP on first connect, and
// refreshes the weather on a timer. The weather fetch itself is blocking
// (~1-2s) but only runs on connect and every few minutes.
void weather_tick();

bool weather_isConnected();          // WiFi associated
bool weather_timeValid();            // NTP has produced a real wall-clock time
const WeatherData& weather_get();
uint32_t weather_updateCount();      // bumps on each successful weather fetch

#endif // WEATHERTIME_H
