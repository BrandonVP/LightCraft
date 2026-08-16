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
    bool    valid;        // true once an OpenWeatherMap fetch has succeeded

    // Room reading from the ESP8266 weather station (LAN).
    int16_t roomTempF;    // deg F
    uint8_t roomHumidity; // %
    bool    roomValid;    // true once the /room endpoint has answered
};

// Start the network task (WiFi + NTP + weather) on core 0. Call once in setup().
// Returns immediately; all blocking work happens on the task, not the UI loop.
void weather_begin();

bool weather_isConnected();          // WiFi associated
bool weather_timeValid();            // NTP has produced a real wall-clock time
WeatherData weather_get();           // thread-safe snapshot of the latest reading
uint32_t weather_updateCount();      // bumps on each successful weather fetch

#endif // WEATHERTIME_H
