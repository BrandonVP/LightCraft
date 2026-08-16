/*
===========================================================================
Name        : WeatherTime.cpp
Author      : Brandon Van Pelt
Description : WiFi + NTP + OpenWeatherMap (see WeatherTime.h). All network work
              runs on a dedicated FreeRTOS task pinned to core 0, so the blocking
              HTTP fetch never stalls the UI loop (core 1). The task publishes
              the latest reading into a mutex-guarded struct the UI reads.
===========================================================================
*/
#include "WeatherTime.h"
#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// US Central time with DST rules (zip 38133 = Arlington/Memphis, TN). Change if
// the OWM_LOCATION in secrets.h moves to another timezone.
static const char* TZ_CENTRAL = "CST6CDT,M3.2.0,M11.1.0";
static const char* NTP_1 = "pool.ntp.org";
static const char* NTP_2 = "time.nist.gov";

static const uint32_t WEATHER_INTERVAL_MS = 5UL * 60UL * 1000UL;   // OpenWeatherMap refresh
static const uint32_t ROOM_INTERVAL_MS    = 45UL * 1000UL;         // poll the local weather station

static const String OWM_URL =
    String("http://api.openweathermap.org/data/2.5/weather?") + OWM_LOCATION + "&appid=" + OWM_API_KEY;

// Shared state. s_w is written by the network task and read by the UI loop, so
// it is guarded by a spinlock (short struct copies, safe across cores).
static WeatherData       s_w = {};
static portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool     s_ntpStarted  = false;
static volatile uint32_t s_updateCount = 0;

static inline int16_t kelvinToF(float k) { return (int16_t)lroundf((k - 273.15f) * 1.8f + 32.0f); }

bool weather_isConnected() { return WiFi.status() == WL_CONNECTED; }

// time() jumps past this (2023-11-14) only after NTP has actually synced.
bool weather_timeValid() { return s_ntpStarted && time(nullptr) > 1700000000; }

uint32_t weather_updateCount() { return s_updateCount; }

WeatherData weather_get()
{
    WeatherData copy;
    portENTER_CRITICAL(&s_mux);
    copy = s_w;
    portEXIT_CRITICAL(&s_mux);
    return copy;
}

// Runs only on the network task (core 0) — the HTTP GET blocks for ~1-2s.
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
            WeatherData w = {};
            w.temperature = kelvinToF((float)doc["main"]["temp"]);
            w.tempHigh    = kelvinToF((float)doc["main"]["temp_max"]);
            w.tempLow     = kelvinToF((float)doc["main"]["temp_min"]);
            w.realFeel    = kelvinToF((float)doc["main"]["feels_like"]);
            w.humidity    = (uint8_t)(doc["main"]["humidity"] | 0);

            const char* name = doc["name"] | "";
            strncpy(w.city, name, sizeof(w.city) - 1);
            w.city[sizeof(w.city) - 1] = '\0';
            w.valid = true;

            portENTER_CRITICAL(&s_mux);
            w.roomTempF    = s_w.roomTempF;    // keep the room reading (owned by fetchRoom)
            w.roomHumidity = s_w.roomHumidity;
            w.roomValid    = s_w.roomValid;
            s_w = w;
            portEXIT_CRITICAL(&s_mux);
            s_updateCount++;

            Serial.printf("[Weather] %dF feels %d hum %u%% hi %d lo %d  %s\n",
                          w.temperature, w.realFeel, w.humidity, w.tempHigh, w.tempLow, w.city);
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

// Poll the ESP8266 weather station on the LAN for the room reading.
static void fetchRoom()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    if (!http.begin(client, ROOM_URL))
        return;

    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (!err && (doc["valid"] | false))
        {
            int16_t t = (int16_t)lroundf((float)doc["tempF"]);
            uint8_t h = (uint8_t)(doc["humidity"] | 0);

            portENTER_CRITICAL(&s_mux);
            s_w.roomTempF    = t;
            s_w.roomHumidity = h;
            s_w.roomValid    = true;
            portEXIT_CRITICAL(&s_mux);
            s_updateCount++;

            Serial.printf("[Room] %dF %u%%\n", t, h);
        }
    }
    else
    {
        Serial.printf("[Room] HTTP GET failed: %d\n", code);
    }
    http.end();
}

// Network task: owns WiFi + NTP + the periodic (blocking) weather/room fetches.
static void weatherTask(void*)
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // WiFi modem power-save glitches the RGB panel scanout — keep the radio steady.
    WiFi.setSleep(false);

    setenv("TZ", TZ_CENTRAL, 1);
    tzset();

    bool     wasConnected  = false;
    bool     everConnected = false;
    uint32_t lastFetchMs   = 0;
    uint32_t lastRoomMs    = 0;

    for (;;)
    {
        bool connected = (WiFi.status() == WL_CONNECTED);

        if (connected != wasConnected)
            Serial.printf("[WiFi] %s  ip=%s\n", connected ? "connected" : "DISCONNECTED",
                          WiFi.localIP().toString().c_str());

        if (connected && !wasConnected)
        {
            configTzTime(TZ_CENTRAL, NTP_1, NTP_2);
            s_ntpStarted = true;
            fetchWeather();  lastFetchMs = millis();
            fetchRoom();     lastRoomMs  = millis();
            everConnected = true;
        }
        wasConnected = connected;

        if (connected && everConnected)
        {
            if (millis() - lastFetchMs >= WEATHER_INTERVAL_MS) { fetchWeather(); lastFetchMs = millis(); }
            if (millis() - lastRoomMs  >= ROOM_INTERVAL_MS)    { fetchRoom();    lastRoomMs  = millis(); }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void weather_begin()
{
    // Pin to core 0 (WiFi/system core); the Arduino loop runs on core 1.
    xTaskCreatePinnedToCore(weatherTask, "weather", 8192, nullptr, 1, nullptr, 0);
}
