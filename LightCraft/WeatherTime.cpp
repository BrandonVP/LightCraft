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

static const uint32_t WEATHER_INTERVAL_MS  = 5UL * 60UL * 1000UL;   // OpenWeatherMap refresh
static const uint32_t ROOM_INTERVAL_MS     = 45UL * 1000UL;         // poll the local weather station
static const uint32_t FORECAST_INTERVAL_MS = 30UL * 60UL * 1000UL;  // 5-day forecast refresh

static const String OWM_URL =
    String("http://api.openweathermap.org/data/2.5/weather?") + OWM_LOCATION + "&appid=" + OWM_API_KEY;

// Free-tier 5 day / 3 hour endpoint. The daily One Call feed needs a separate
// subscription, so the days below are folded from these 3-hourly slots.
static const String OWM_FORECAST_URL =
    String("http://api.openweathermap.org/data/2.5/forecast?") + OWM_LOCATION + "&appid=" + OWM_API_KEY;

// Shared state. s_w is written by the network task and read by the UI loop, so
// it is guarded by a spinlock (short struct copies, safe across cores).
static WeatherData       s_w = {};
static portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool     s_ntpStarted  = false;
static volatile uint32_t s_updateCount = 0;

static ForecastDay       s_days[FORECAST_DAYS] = {};
static volatile uint32_t s_forecastCount = 0;
static volatile bool     s_forecastWanted = false;
static volatile uint32_t s_forecastStampMs = 0;

// A forecast younger than this is good enough to show as-is; opening the page
// then costs no fetch, and no on-screen update.
static const uint32_t FORECAST_FRESH_MS = 10UL * 60UL * 1000UL;

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

void forecast_get(ForecastDay out[FORECAST_DAYS])
{
    portENTER_CRITICAL(&s_mux);
    memcpy(out, s_days, sizeof(s_days));
    portEXIT_CRITICAL(&s_mux);
}

uint32_t forecast_updateCount() { return s_forecastCount; }
bool     forecast_isValid()     { return s_forecastCount > 0; }

void forecast_request()
{
    // Opening the forecast page should not re-fetch a forecast that is still
    // current: the refresh would only redraw the same numbers.
    if (s_forecastCount > 0 && (millis() - s_forecastStampMs) < FORECAST_FRESH_MS)
        return;

    s_forecastWanted = true;
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

            // Condition group + icon code drive the drawn icon on the Home card.
            const char* cond = doc["weather"][0]["main"] | "";
            strncpy(w.condition, cond, sizeof(w.condition) - 1);
            w.condition[sizeof(w.condition) - 1] = '\0';

            const char* icon = doc["weather"][0]["icon"] | "";
            strncpy(w.iconCode, icon, sizeof(w.iconCode) - 1);
            w.iconCode[sizeof(w.iconCode) - 1] = '\0';

            w.valid = true;

            portENTER_CRITICAL(&s_mux);
            w.roomTempF    = s_w.roomTempF;    // keep the room reading (owned by fetchRoom)
            w.roomHumidity = s_w.roomHumidity;
            w.roomValid    = s_w.roomValid;
            w.roomStampMs  = s_w.roomStampMs;
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

// Fold the 3-hourly slots into local calendar days. Runs only on the network
// task. Needs a real wall clock: the day a slot belongs to comes from
// localtime(), so this is skipped until NTP has synced.
static void fetchForecast()
{
    if (WiFi.status() != WL_CONNECTED || !weather_timeValid())
        return;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(8000);
    if (!http.begin(client, OWM_FORECAST_URL))
        return;

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        Serial.printf("[Forecast] HTTP GET failed: %d\n", code);
        http.end();
        return;
    }

    // The full response is ~50 KB of JSON. A filter keeps only the four fields
    // used here, so what lands in memory is a few KB instead.
    JsonDocument filter;
    filter["list"][0]["dt"] = true;
    filter["list"][0]["main"]["temp_min"] = true;
    filter["list"][0]["main"]["temp_max"] = true;
    filter["list"][0]["weather"][0]["icon"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err)
    {
        Serial.printf("[Forecast] JSON parse failed: %s\n", err.c_str());
        return;
    }

    ForecastDay days[FORECAST_DAYS] = {};
    int  keyYday[FORECAST_DAYS] = {};
    int  iconDistance[FORECAST_DAYS] = {};   // |hour - 13|, to pick a midday icon
    int  dayCount = 0;

    for (JsonObject slot : doc["list"].as<JsonArray>())
    {
        time_t    dt = (time_t)slot["dt"].as<uint32_t>();
        struct tm lt;
        localtime_r(&dt, &lt);

        int idx = -1;
        for (int i = 0; i < dayCount; i++)
            if (keyYday[i] == lt.tm_yday) { idx = i; break; }

        if (idx < 0)
        {
            if (dayCount >= FORECAST_DAYS)
                continue;
            idx = dayCount++;
            keyYday[idx]      = lt.tm_yday;
            iconDistance[idx] = 99;
            days[idx].valid    = true;
            days[idx].wday     = (uint8_t)lt.tm_wday;
            days[idx].tempHigh = INT16_MIN;
            days[idx].tempLow  = INT16_MAX;
        }

        int16_t hi = kelvinToF((float)slot["main"]["temp_max"]);
        int16_t lo = kelvinToF((float)slot["main"]["temp_min"]);
        if (hi > days[idx].tempHigh) days[idx].tempHigh = hi;
        if (lo < days[idx].tempLow)  days[idx].tempLow  = lo;

        int distance = abs(lt.tm_hour - 13);
        if (distance < iconDistance[idx])
        {
            iconDistance[idx] = distance;
            const char* icon = slot["weather"][0]["icon"] | "";
            strncpy(days[idx].iconCode, icon, sizeof(days[idx].iconCode) - 1);
            days[idx].iconCode[sizeof(days[idx].iconCode) - 1] = '\0';
        }
    }

    if (dayCount == 0)
    {
        Serial.println("[Forecast] response held no usable slots");
        return;
    }

    portENTER_CRITICAL(&s_mux);
    memcpy(s_days, days, sizeof(s_days));
    portEXIT_CRITICAL(&s_mux);
    s_forecastStampMs = millis();
    s_forecastCount++;

    Serial.printf("[Forecast] %d days:", dayCount);
    for (int i = 0; i < dayCount; i++)
        Serial.printf("  %d/%d %s", days[i].tempHigh, days[i].tempLow, days[i].iconCode);
    Serial.println();
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
            s_w.roomStampMs  = millis();
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

    bool     wasConnected   = false;
    bool     everConnected  = false;
    uint32_t lastFetchMs    = 0;
    uint32_t lastRoomMs     = 0;
    uint32_t lastForecastMs = 0;

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

            // The forecast refreshes slowly, or straight away when the UI opens
            // the forecast page (or when NTP lands and the first try was skipped).
            bool wanted = s_forecastWanted;
            if (wanted || s_forecastCount == 0 || (millis() - lastForecastMs) >= FORECAST_INTERVAL_MS)
            {
                // Don't retry the first fetch faster than the room poll.
                if (wanted || (millis() - lastForecastMs) >= ROOM_INTERVAL_MS)
                {
                    s_forecastWanted = false;
                    fetchForecast();
                    lastForecastMs = millis();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void weather_begin()
{
    // Pin to core 0 (WiFi/system core); the Arduino loop runs on core 1.
    // 12 KB: the forecast response is the deepest parse this task does.
    xTaskCreatePinnedToCore(weatherTask, "weather", 12288, nullptr, 1, nullptr, 0);
}
