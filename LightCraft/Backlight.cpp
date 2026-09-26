/*
===========================================================================
Name        : Backlight.cpp
Author      : Brandon Van Pelt
Description : Backlight dimming and touch wake (see Backlight.h).
===========================================================================
*/
#include "Backlight.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "lightcraft";
static const char* NVS_KEY_BL    = "backlight";
static const uint8_t BLOB_VERSION = 1;

// 5 kHz is well clear of anything the eye can see and of the panel's ~49 Hz
// refresh, and high enough not to whine. 10 bits is far finer than the eye can
// resolve on a backlight.
static const uint32_t PWM_FREQ_HZ = 5000;
static const uint8_t  PWM_BITS    = 10;
static const uint16_t DUTY_MAX    = (1 << PWM_BITS) - 1;

// Fade is stepped here rather than handed to ledcFade(): the hardware fade
// leans on an ISR, and a non-IRAM-safe ISR is exactly what crashed this board
// once already (the RGB bounce buffer).
static const uint32_t FADE_INTERVAL_MS = 16;    // ~60 steps a second
static const uint32_t SAVE_SETTLE_MS   = 1500;  // coalesce edits before writing

struct BlBlob {
    uint8_t  version;
    uint8_t  activePct;
    uint8_t  idlePct;
    uint16_t idleSeconds;
};

static const uint16_t    TIMEOUTS[]      = { 0, 10, 20, 30, 60, 120, 300, 600 };
static const char* const TIMEOUT_NAMES[] = { "Off", "10s", "20s", "30s", "1m", "2m", "5m", "10m" };
static const uint8_t     TIMEOUT_COUNT   = (uint8_t)(sizeof(TIMEOUTS) / sizeof(TIMEOUTS[0]));

static uint8_t  s_pin = 0;
static uint8_t  s_activePct   = 100;
static uint8_t  s_idlePct     = 15;
static uint16_t s_idleSeconds = 30;

static uint16_t s_duty       = 0;      // what the pin is showing
static uint16_t s_targetDuty = 0;
static uint32_t s_lastFadeMs = 0;
static uint32_t s_lastTouchMs = 0;
static bool     s_wasTouched = false;
static bool     s_swallow    = false;

static bool     s_dirty      = false;
static uint32_t s_changedMs  = 0;

// Perceived brightness is roughly the square of duty, so square the percentage
// on the way in. Without this the bottom half of the range is all one shade and
// a "15%" idle level still lights the room.
static uint16_t dutyFor(uint8_t pct)
{
    if (pct == 0)
        return 0;
    uint32_t d = ((uint32_t)DUTY_MAX * pct * pct) / (100UL * 100UL);
    return (uint16_t)(d == 0 ? 1 : d);
}

static void clampSettings(void)
{
    if (s_activePct < BL_ACTIVE_MIN) s_activePct = BL_ACTIVE_MIN;
    if (s_activePct > BL_ACTIVE_MAX) s_activePct = BL_ACTIVE_MAX;
    if (s_idlePct > BL_IDLE_MAX)     s_idlePct = BL_IDLE_MAX;
    // An idle level above the active one would brighten the screen when left
    // alone, which is backwards.
    if (s_idlePct > s_activePct)     s_idlePct = s_activePct;

    bool known = false;
    for (uint8_t i = 0; i < TIMEOUT_COUNT; i++)
        if (TIMEOUTS[i] == s_idleSeconds) { known = true; break; }
    if (!known) s_idleSeconds = 30;
}

static void save(void)
{
    BlBlob blob = {};
    blob.version     = BLOB_VERSION;
    blob.activePct   = s_activePct;
    blob.idlePct     = s_idlePct;
    blob.idleSeconds = s_idleSeconds;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
    {
        Serial.println("[Backlight] save failed: cannot open NVS");
        return;
    }
    prefs.putBytes(NVS_KEY_BL, &blob, sizeof(blob));
    prefs.end();
}

static void markDirty(void)
{
    s_dirty = true;
    s_changedMs = millis();
}

void BACKLIGHT_begin(uint8_t pin)
{
    s_pin = pin;

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true /* read-only */))
    {
        BlBlob blob = {};
        size_t got = prefs.getBytes(NVS_KEY_BL, &blob, sizeof(blob));
        prefs.end();

        if (got == sizeof(blob) && blob.version == BLOB_VERSION)
        {
            s_activePct   = blob.activePct;
            s_idlePct     = blob.idlePct;
            s_idleSeconds = blob.idleSeconds;
        }
    }
    clampSettings();

    ledcAttach(s_pin, PWM_FREQ_HZ, PWM_BITS);

    // Come up lit, and start the idle clock so an untouched panel dims on its
    // own after a boot.
    s_duty = s_targetDuty = dutyFor(s_activePct);
    ledcWrite(s_pin, s_duty);
    s_lastTouchMs = millis();
    s_lastFadeMs  = millis();

    Serial.printf("[Backlight] active %u%%, idle %u%% after %us\n",
                  s_activePct, s_idlePct, s_idleSeconds);
}

void BACKLIGHT_tick(bool touched)
{
    const uint32_t now = millis();

    // --- Touch: wake, and decide whether to eat this tap -------------------
    if (touched)
    {
        // Only the transition matters for swallowing: a tap that arrives while
        // the panel is dark is the user finding the screen, not pressing what
        // happens to be under their finger.
        if (!s_wasTouched && s_duty == 0)
            s_swallow = true;

        s_lastTouchMs = now;
        s_targetDuty = dutyFor(s_activePct);
    }
    else if (s_wasTouched)
    {
        s_swallow = false;              // finger lifted; the next tap counts
    }
    s_wasTouched = touched;

    // --- Idle ---------------------------------------------------------------
    if (!touched && s_idleSeconds != 0 &&
        (now - s_lastTouchMs) >= (uint32_t)s_idleSeconds * 1000UL)
    {
        s_targetDuty = dutyFor(s_idlePct);
    }

    // --- Fade ---------------------------------------------------------------
    if (s_duty != s_targetDuty && (now - s_lastFadeMs) >= FADE_INTERVAL_MS)
    {
        s_lastFadeMs = now;

        int32_t diff = (int32_t)s_targetDuty - (int32_t)s_duty;
        int32_t step = diff / 4;                      // ease out
        if (step == 0) step = (diff > 0) ? 1 : -1;    // always finish

        s_duty = (uint16_t)((int32_t)s_duty + step);
        ledcWrite(s_pin, s_duty);
    }

    // --- Deferred save ------------------------------------------------------
    if (s_dirty && (now - s_changedMs) >= SAVE_SETTLE_MS)
    {
        s_dirty = false;
        save();
    }
}

bool BACKLIGHT_swallowTouch(void) { return s_swallow; }

uint8_t  BACKLIGHT_activePct(void)   { return s_activePct; }
uint8_t  BACKLIGHT_idlePct(void)     { return s_idlePct; }
uint16_t BACKLIGHT_idleSeconds(void) { return s_idleSeconds; }

void BACKLIGHT_setActivePct(uint8_t pct)
{
    if (pct < BL_ACTIVE_MIN) pct = BL_ACTIVE_MIN;
    if (pct > BL_ACTIVE_MAX) pct = BL_ACTIVE_MAX;
    if (pct == s_activePct) return;

    s_activePct = pct;
    if (s_idlePct > s_activePct) s_idlePct = s_activePct;

    // The user is on the settings page, so they are looking at the active
    // level: show the change as they make it.
    s_targetDuty = dutyFor(s_activePct);
    markDirty();
}

void BACKLIGHT_setIdlePct(uint8_t pct)
{
    if (pct > BL_IDLE_MAX)   pct = BL_IDLE_MAX;
    if (pct > s_activePct)   pct = s_activePct;
    if (pct == s_idlePct) return;

    s_idlePct = pct;
    markDirty();
}

void BACKLIGHT_setIdleSeconds(uint16_t seconds)
{
    if (seconds == s_idleSeconds) return;
    s_idleSeconds = seconds;
    clampSettings();
    markDirty();
}

uint8_t BACKLIGHT_timeoutCount(void) { return TIMEOUT_COUNT; }

uint8_t BACKLIGHT_timeoutIndex(void)
{
    for (uint8_t i = 0; i < TIMEOUT_COUNT; i++)
        if (TIMEOUTS[i] == s_idleSeconds)
            return i;
    return 0;
}

void BACKLIGHT_setTimeoutIndex(uint8_t index)
{
    if (index >= TIMEOUT_COUNT) return;
    BACKLIGHT_setIdleSeconds(TIMEOUTS[index]);
}

const char* BACKLIGHT_timeoutName(uint8_t index)
{
    return (index < TIMEOUT_COUNT) ? TIMEOUT_NAMES[index] : "";
}
