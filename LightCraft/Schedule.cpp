/*
===========================================================================
Name        : Schedule.cpp
Author      : Brandon Van Pelt
Description : Light schedules (see Schedule.h).
===========================================================================
*/
#include "Schedule.h"
#include "WeatherTime.h"

#include <Preferences.h>
#include <time.h>
#include <stdio.h>

static const char* NVS_NAMESPACE = "lightcraft";
static const char* NVS_KEY_SCHED = "schedules";
static const uint8_t BLOB_VERSION = 1;

struct SchedBlob {
    uint8_t       version;
    uint8_t       count;
    LightSchedule light[LIGHT_COUNT];
};

static const uint32_t EVAL_INTERVAL_MS = 5000;
static const uint32_t SAVE_SETTLE_MS   = 1500;   // coalesce edits before writing

static LightSchedule s_sched[LIGHT_COUNT];
static bool     s_dirty = false;
static uint32_t s_changedMs = 0;
static uint32_t s_lastEvalMs = 0;

// Edits save themselves once the user stops adjusting, so the editor needs no
// save button and a schedule cannot be lost by walking away from the panel.
static void markDirty(void)
{
    s_dirty = true;
    s_changedMs = millis();
}

// Minute of day at the previous evaluation. 0xFFFF means "not seeded yet" — the
// first pass only records where the clock is, so a reboot does not replay every
// event that happens to be behind us today.
static uint16_t s_lastMinute = 0xFFFF;

static void defaults(void)
{
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        s_sched[i].on  = { SCHED_NONE, 18, 0, 0 };
        s_sched[i].off = { SCHED_NONE, 23, 0, 0 };
    }
}

static void clampEvent(SchedEvent& e)
{
    if (e.trigger >= SCHED_TRIGGER_COUNT) e.trigger = SCHED_NONE;
    if (e.hour > 23)   e.hour = 0;
    if (e.minute > 59) e.minute = 0;
    e.minute = (uint8_t)((e.minute / 5) * 5);
    if (e.offsetQ >  SCHED_OFFSET_Q_MAX) e.offsetQ =  SCHED_OFFSET_Q_MAX;
    if (e.offsetQ < -SCHED_OFFSET_Q_MAX) e.offsetQ = -SCHED_OFFSET_Q_MAX;
}

void SCHED_begin(void)
{
    defaults();

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true /* read-only */))
    {
        Serial.println("[Sched] no saved schedules - using defaults");
        return;
    }

    SchedBlob blob = {};
    size_t got = prefs.getBytes(NVS_KEY_SCHED, &blob, sizeof(blob));
    prefs.end();

    if (got != sizeof(blob) || blob.version != BLOB_VERSION || blob.count != LIGHT_COUNT)
    {
        Serial.printf("[Sched] saved schedules unusable (%u bytes, v%u) - using defaults\n",
                      (unsigned)got, blob.version);
        return;
    }

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        s_sched[i] = blob.light[i];
        clampEvent(s_sched[i].on);
        clampEvent(s_sched[i].off);

        char line[48];
        SCHED_summary(i, line, sizeof(line));
        Serial.printf("[Sched] %s: %s\n", RELAY_name(i), line);
    }

    s_dirty = false;
}

bool SCHED_save(void)
{
    SchedBlob blob = {};
    blob.version = BLOB_VERSION;
    blob.count   = LIGHT_COUNT;
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        blob.light[i] = s_sched[i];

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
    {
        Serial.println("[Sched] save failed: cannot open NVS");
        return false;
    }
    size_t written = prefs.putBytes(NVS_KEY_SCHED, &blob, sizeof(blob));
    prefs.end();

    if (written != sizeof(blob))
        return false;

    s_dirty = false;
    Serial.println("[Sched] schedules saved");
    return true;
}

bool SCHED_isDirty(void) { return s_dirty; }

LightSchedule SCHED_get(uint8_t light)
{
    if (light >= LIGHT_COUNT)
    {
        LightSchedule none = {};
        return none;
    }
    return s_sched[light];
}

static SchedEvent* eventOf(uint8_t light, bool onEvent)
{
    if (light >= LIGHT_COUNT) return nullptr;
    return onEvent ? &s_sched[light].on : &s_sched[light].off;
}

void SCHED_setTrigger(uint8_t light, bool onEvent, uint8_t trigger)
{
    SchedEvent* e = eventOf(light, onEvent);
    if (!e || trigger >= SCHED_TRIGGER_COUNT || e->trigger == trigger) return;
    e->trigger = trigger;
    markDirty();
}

void SCHED_adjustHour(uint8_t light, bool onEvent, int delta)
{
    SchedEvent* e = eventOf(light, onEvent);
    if (!e) return;
    e->hour = (uint8_t)((e->hour + 24 + (delta % 24)) % 24);
    markDirty();
}

void SCHED_adjustMinute(uint8_t light, bool onEvent, int delta)
{
    SchedEvent* e = eventOf(light, onEvent);
    if (!e) return;
    int m = e->minute + delta * 5;
    while (m < 0)   m += 60;
    while (m >= 60) m -= 60;
    e->minute = (uint8_t)m;
    markDirty();
}

void SCHED_adjustOffset(uint8_t light, bool onEvent, int deltaQ)
{
    SchedEvent* e = eventOf(light, onEvent);
    if (!e) return;
    int q = e->offsetQ + deltaQ;
    if (q >  SCHED_OFFSET_Q_MAX) q =  SCHED_OFFSET_Q_MAX;
    if (q < -SCHED_OFFSET_Q_MAX) q = -SCHED_OFFSET_Q_MAX;
    if (q == e->offsetQ) return;
    e->offsetQ = (int8_t)q;
    markDirty();
}

const char* SCHED_triggerName(uint8_t trigger)
{
    switch (trigger)
    {
        case SCHED_TIME:    return "Time";
        case SCHED_SUNRISE: return "Sunrise";
        case SCHED_SUNSET:  return "Sunset";
        default:            return "Off";
    }
}

int SCHED_resolveMinutes(const SchedEvent& e)
{
    if (e.trigger == SCHED_TIME)
        return e.hour * 60 + e.minute;

    if (e.trigger != SCHED_SUNRISE && e.trigger != SCHED_SUNSET)
        return -1;

    WeatherData w = weather_get();
    uint32_t epoch = (e.trigger == SCHED_SUNRISE) ? w.sunrise : w.sunset;
    if (!w.valid || epoch == 0)
        return -1;                      // no fix on the sun times yet

    time_t    t = (time_t)epoch;
    struct tm lt;
    localtime_r(&t, &lt);

    int m = lt.tm_hour * 60 + lt.tm_min + (int)e.offsetQ * 15;
    while (m < 0)     m += 24 * 60;
    while (m >= 1440) m -= 24 * 60;
    return m;
}

// One event, as text: "23:00", "sunset-15", "sunrise+30" or "off".
static void describe(const SchedEvent& e, char* out, size_t n)
{
    switch (e.trigger)
    {
        case SCHED_TIME:
            snprintf(out, n, "%02u:%02u", e.hour, e.minute);
            break;
        case SCHED_SUNRISE:
        case SCHED_SUNSET:
        {
            const char* base = (e.trigger == SCHED_SUNRISE) ? "sunrise" : "sunset";
            int mins = (int)e.offsetQ * 15;
            if (mins == 0) snprintf(out, n, "%s", base);
            else           snprintf(out, n, "%s%+d", base, mins);
            break;
        }
        default:
            snprintf(out, n, "off");
            break;
    }
}

void SCHED_summary(uint8_t light, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    if (light >= LIGHT_COUNT) { out[0] = '\0'; return; }

    const LightSchedule& s = s_sched[light];
    if (s.on.trigger == SCHED_NONE && s.off.trigger == SCHED_NONE)
    {
        snprintf(out, outSize, "no schedule");
        return;
    }

    char onText[16], offText[16];
    describe(s.on, onText, sizeof(onText));
    describe(s.off, offText, sizeof(offText));
    snprintf(out, outSize, "On %s  Off %s", onText, offText);
}

// Did the clock step over minute `t` between the previous pass and this one?
// Window is (a, b], and wraps past midnight.
static bool crossed(uint16_t a, uint16_t b, int t)
{
    if (t < 0 || a == b) return false;
    const uint16_t tt = (uint16_t)t;
    if (a < b) return (tt > a && tt <= b);
    return (tt > a || tt <= b);
}

void SCHED_tick(void)
{
    // Deferred save, checked before the clock guard below: an edit must survive
    // even if NTP has not come up yet.
    if (s_dirty && (millis() - s_changedMs) >= SAVE_SETTLE_MS)
        SCHED_save();

    if (millis() - s_lastEvalMs < EVAL_INTERVAL_MS)
        return;
    s_lastEvalMs = millis();

    // Without a real clock every event time is meaningless.
    if (!weather_timeValid())
        return;

    time_t    now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    const uint16_t nowMin = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);

    if (s_lastMinute == 0xFFFF)
    {
        s_lastMinute = nowMin;          // seed only; never fire on the first pass
        return;
    }
    if (nowMin == s_lastMinute)
        return;

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        const int onMin  = SCHED_resolveMinutes(s_sched[i].on);
        const int offMin = SCHED_resolveMinutes(s_sched[i].off);

        if (crossed(s_lastMinute, nowMin, onMin) && !RELAY_isOn(i))
        {
            RELAY_set(i, true);
            Serial.printf("[Sched] %s ON (%02d:%02d)\n", RELAY_name(i), onMin / 60, onMin % 60);
        }
        if (crossed(s_lastMinute, nowMin, offMin) && RELAY_isOn(i))
        {
            RELAY_set(i, false);
            Serial.printf("[Sched] %s OFF (%02d:%02d)\n", RELAY_name(i), offMin / 60, offMin % 60);
        }
    }

    s_lastMinute = nowMin;
}
