/*
===========================================================================
Name        : WeatherTime.cpp
Author      : Brandon Van Pelt
Description : WiFi + NTP + OpenWeatherMap (see WeatherTime.h). Ported from the
              old ESP8266 weather station: same API key/URL and Kelvin->F math,
              now on the ESP32-S3 core (WiFi.h / HTTPClient.h) with NTP for time.
===========================================================================
*/
#include "WeatherTime.h"
#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// US Central time with DST rules (zip 38002 = Memphis, TN). Change if the
// OWM_LOCATION in secrets.h moves to another timezone.
static const char* TZ_CENTRAL = "CST6CDT,M3.2.0,M11.1.0";
static const char* NTP_1 = "pool.ntp.org";
static const char* NTP_2 = "time.nist.gov";

static const uint32_t WEATHER_INTERVAL_MS = 5UL * 60UL * 1000UL;   // refresh every 5 min

static const String OWM_URL =
    String("http://api.openweathermap.org/data/2.5/weather?") + OWM_LOCATION + "&appid=" + OWM_API_KEY;

static WeatherData s_w = {};
static bool        s_ntpStarted   = false;
static bool        s_everFetched  = false;
static uint32_t    s_lastFetchMs  = 0;
static uint32_t    s_updateCount  = 0;

static inline int16_t kelvinToF(float k) { return (int16_t)lroundf((k - 273.15f) * 1.8f + 32.0f); }

void weather_begin()
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // WiFi modem power-save periodically bursts the bus and glitches the RGB
    // panel's PSRAM scanout (diagonal tearing) — keep the radio steady.
    WiFi.setSleep(false);

    // Set the timezone up front so localtime() is correct as soon as NTP syncs.
    setenv("TZ", TZ_CENTRAL, 1);
    tzset();
}

bool weather_isConnected() { return WiFi.status() == WL_CONNECTED; }

// time() jumps past this (2023-11-14) only after NTP has actually synced.
bool weather_timeValid() { return s_ntpStarted && time(nullptr) > 1700000000; }

const WeatherData& weather_get() { return s_w; }
uint32_t weather_updateCount()   { return s_updateCount; }

static void fetchWeather()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(5000);
    if (!http.begin(client, OWM_URL))
        return;

    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (!err)
        {
            s_w.temperature = kelvinToF((float)doc["main"]["temp"]);
            s_w.tempHigh    = kelvinToF((float)doc["main"]["temp_max"]);
            s_w.tempLow     = kelvinToF((float)doc["main"]["temp_min"]);
            s_w.realFeel    = kelvinToF((float)doc["main"]["feels_like"]);
            s_w.humidity    = (uint8_t)(doc["main"]["humidity"] | 0);

            const char* name = doc["name"] | "";
            strncpy(s_w.city, name, sizeof(s_w.city) - 1);
            s_w.city[sizeof(s_w.city) - 1] = '\0';

            s_w.valid = true;
            s_updateCount++;
            Serial.printf("[Weather] %dF feels %d hum %u%% hi %d lo %d  %s\n",
                          s_w.temperature, s_w.realFeel, s_w.humidity,
                          s_w.tempHigh, s_w.tempLow, s_w.city);
        }
        else
        {
            Serial.printf("[Weather] JSON parse failed: %s\n", err.c_str());
        }
    }
    else
    {
        Serial.printf("[Weather] HTTP GET failed: %d\n", code);
    }
    http.end();
}

void weather_tick()
{
    static bool     wasConnected    = false;
    static uint32_t lastReconnectMs = 0;

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected != wasConnected)
        Serial.printf("[WiFi] %s  ip=%s\n", connected ? "connected" : "DISCONNECTED",
                      WiFi.localIP().toString().c_str());

    if (connected && !wasConnected)
    {
        // Just associated: kick off NTP and grab the first weather reading.
        configTzTime(TZ_CENTRAL, NTP_1, NTP_2);
        s_ntpStarted = true;
        fetchWeather();
        s_lastFetchMs = millis();
        s_everFetched = true;
    }
    wasConnected = connected;

    // Nudge a reconnect while down (non-blocking).
    if (!connected && (millis() - lastReconnectMs > 15000))
    {
        Serial.println(F("[WiFi] reconnecting..."));
        WiFi.reconnect();
        lastReconnectMs = millis();
    }

    if (connected && s_everFetched && (millis() - s_lastFetchMs >= WEATHER_INTERVAL_MS))
    {
        fetchWeather();
        s_lastFetchMs = millis();
    }
}
