/*
===========================================================================
Name        : TempControl.cpp
Author      : Brandon Van Pelt
Description : Room-temperature automation (see TempControl.h).
===========================================================================
*/
#include "TempControl.h"
#include "WeatherTime.h"

#include <Preferences.h>

// NVS namespace + key. The whole rule table is one blob, so a save is a single
// atomic-enough write instead of six separate keys that could tear apart.
static const char* NVS_NAMESPACE = "lightcraft";
static const char* NVS_KEY_RULES = "temprules";
static const uint8_t BLOB_VERSION = 1;

struct RuleBlob {
    uint8_t  version;
    uint8_t  count;
    TempRule rules[LIGHT_COUNT];
};

// How often the rules are evaluated, and how old a room reading may be before
// it stops driving them (the station is polled every 45s).
static const uint32_t EVAL_INTERVAL_MS = 2000;
static const uint32_t ROOM_MAX_AGE_MS  = 5UL * 60UL * 1000UL;

// Quiet period after an edit. Holding -/+ sweeps the setpoint straight past the
// room temperature; without this the relay would click on every crossing.
static const uint32_t EDIT_SETTLE_MS = 1500;

static TempRule s_rules[LIGHT_COUNT];

// Per-light rule output, held across ticks so the relay is driven only on the
// transition. s_haveState is cleared whenever a rule is edited, which makes the
// next evaluation an unconditional apply of the new rule.
static bool s_ruleOn[LIGHT_COUNT];
static bool s_haveState[LIGHT_COUNT];

static bool     s_dirty = false;
static uint32_t s_lastEvalMs = 0;
static bool     s_editPending = false;   // an edit is still settling
static uint32_t s_lastEditMs  = 0;

static void resetState(uint8_t light)
{
    s_ruleOn[light]    = false;
    s_haveState[light] = false;
}

// An edited rule takes effect immediately (SAVE only makes it survive a reboot),
// but not until the user has stopped turning the dial.
static void noteEdit(uint8_t light)
{
    resetState(light);            // re-apply the new rule on the next evaluation
    s_editPending = true;
    s_lastEditMs  = millis();
    s_dirty       = true;
}

static void defaults(void)
{
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        s_rules[i].mode      = TEMP_MODE_OFF;
        s_rules[i].setpointF = 74;
        resetState(i);
    }
}

void TEMPCTL_begin(void)
{
    defaults();

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true /* read-only */))
    {
        Serial.println("[TempCtl] no saved rules (namespace missing) — using defaults");
        return;
    }

    RuleBlob blob = {};
    size_t got = prefs.getBytes(NVS_KEY_RULES, &blob, sizeof(blob));
    prefs.end();

    if (got != sizeof(blob) || blob.version != BLOB_VERSION || blob.count != LIGHT_COUNT)
    {
        Serial.printf("[TempCtl] saved rules unusable (%u bytes, v%u) — using defaults\n",
                      (unsigned)got, blob.version);
        return;
    }

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        // Clamp on load: a blob written by an older/newer build must never put
        // the UI or the relays into an out-of-range state.
        s_rules[i].mode = (blob.rules[i].mode < TEMP_MODE_COUNT) ? blob.rules[i].mode : TEMP_MODE_OFF;
        int16_t sp = blob.rules[i].setpointF;
        if (sp < TEMPCTL_MIN_F) sp = TEMPCTL_MIN_F;
        if (sp > TEMPCTL_MAX_F) sp = TEMPCTL_MAX_F;
        s_rules[i].setpointF = sp;

        Serial.printf("[TempCtl] %s: %s %d\xF8" "F\n",
                      RELAY_name(i), TEMPCTL_modeName(s_rules[i].mode), s_rules[i].setpointF);
    }

    s_dirty = false;
}

bool TEMPCTL_save(void)
{
    RuleBlob blob = {};
    blob.version = BLOB_VERSION;
    blob.count   = LIGHT_COUNT;
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        blob.rules[i] = s_rules[i];

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
    {
        Serial.println("[TempCtl] save failed: cannot open NVS");
        return false;
    }

    size_t written = prefs.putBytes(NVS_KEY_RULES, &blob, sizeof(blob));
    prefs.end();

    if (written != sizeof(blob))
    {
        Serial.printf("[TempCtl] save failed: wrote %u of %u bytes\n",
                      (unsigned)written, (unsigned)sizeof(blob));
        return false;
    }

    s_dirty = false;
    Serial.println("[TempCtl] rules saved");
    return true;
}

TempRule TEMPCTL_get(uint8_t light)
{
    if (light >= LIGHT_COUNT)
    {
        TempRule none = { TEMP_MODE_OFF, 0 };
        return none;
    }
    return s_rules[light];
}

void TEMPCTL_setMode(uint8_t light, uint8_t mode)
{
    if (light >= LIGHT_COUNT || mode >= TEMP_MODE_COUNT) return;
    if (s_rules[light].mode == mode) return;

    s_rules[light].mode = mode;
    noteEdit(light);
}

void TEMPCTL_setSetpoint(uint8_t light, int16_t setpointF)
{
    if (light >= LIGHT_COUNT) return;
    if (setpointF < TEMPCTL_MIN_F) setpointF = TEMPCTL_MIN_F;
    if (setpointF > TEMPCTL_MAX_F) setpointF = TEMPCTL_MAX_F;
    if (s_rules[light].setpointF == setpointF) return;

    s_rules[light].setpointF = setpointF;
    noteEdit(light);
}

bool TEMPCTL_isDirty(void) { return s_dirty; }

const char* TEMPCTL_modeName(uint8_t mode)
{
    switch (mode)
    {
        case TEMP_MODE_ABOVE: return "ABOVE";
        case TEMP_MODE_BELOW: return "BELOW";
        default:              return "OFF";
    }
}

// Rule output for one light, with the deadband applied on the release side.
static bool evaluate(uint8_t light, int16_t roomF)
{
    const TempRule& r = s_rules[light];
    bool on = s_ruleOn[light];

    if (r.mode == TEMP_MODE_ABOVE)
    {
        if (!on && roomF >= r.setpointF)                             on = true;
        else if (on && roomF <= r.setpointF - TEMPCTL_HYSTERESIS_F)  on = false;
    }
    else if (r.mode == TEMP_MODE_BELOW)
    {
        if (!on && roomF <= r.setpointF)                             on = true;
        else if (on && roomF >= r.setpointF + TEMPCTL_HYSTERESIS_F)  on = false;
    }

    return on;
}

void TEMPCTL_tick(void)
{
    if (millis() - s_lastEvalMs < EVAL_INTERVAL_MS)
        return;
    s_lastEvalMs = millis();

    if (s_editPending)
    {
        if (millis() - s_lastEditMs < EDIT_SETTLE_MS)
            return;               // still turning the dial
        s_editPending = false;
    }

    WeatherData w = weather_get();
    if (!w.roomValid || (millis() - w.roomStampMs) > ROOM_MAX_AGE_MS)
        return;                   // no trustworthy reading: leave the relays alone

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        if (s_rules[i].mode == TEMP_MODE_OFF)
        {
            resetState(i);        // disabling a rule never moves the relay
            continue;
        }

        bool want  = evaluate(i, w.roomTempF);
        bool first = !s_haveState[i];

        // Drive the relay on the crossing only (and once when a rule first sees
        // a reading), so a manual toggle in between is left standing.
        if (first || want != s_ruleOn[i])
        {
            s_ruleOn[i]    = want;
            s_haveState[i] = true;

            if (RELAY_isOn(i) != want)
            {
                RELAY_set(i, want);
                Serial.printf("[TempCtl] %s -> %s (room %d\xF8" "F, %s %d\xF8" "F)\n",
                              RELAY_name(i), want ? "ON" : "OFF", w.roomTempF,
                              TEMPCTL_modeName(s_rules[i].mode), s_rules[i].setpointF);
            }
        }
    }
}
