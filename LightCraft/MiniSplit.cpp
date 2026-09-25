/*
===========================================================================
Name        : MiniSplit.cpp
Author      : Brandon Van Pelt
Description : Mini-split shadow state (see MiniSplit.h).
===========================================================================
*/
#include "MiniSplit.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "lightcraft";
static const char* NVS_KEY_STATE = "minisplit";
static const uint8_t BLOB_VERSION = 1;

struct StateBlob {
    uint8_t        version;
    MiniSplitState state;
};

// A press sends the whole state, so a burst of taps (three on the setpoint)
// should become one frame, not three. Wait for the user to stop first.
static const uint32_t SETTLE_MS = 700;

// Re-send the state periodically even when nothing changed: IR is open loop and
// the unit may have missed a frame, or been changed from the handheld remote.
static const uint32_t REFRESH_MS = 10UL * 60UL * 1000UL;

static MiniSplitState s_state;
static bool           s_pending   = false;   // change waiting to be sent
static uint32_t       s_changedMs = 0;
static uint32_t       s_sentMs    = 0;
static bool           s_linked    = false;   // set by the transport, once it exists

static void defaults(void)
{
    s_state.power     = false;
    s_state.mode      = MS_MODE_COOL;
    s_state.setpointF = 72;
    s_state.fan       = MS_FAN_AUTO;
    s_state.swingV    = MS_SWING_FIXED;
    s_state.swingH    = MS_SWING_FIXED;
}

static void clampState(MiniSplitState& s)
{
    if (s.mode   >= MS_MODE_COUNT)  s.mode   = MS_MODE_COOL;
    if (s.fan    >= MS_FAN_COUNT)   s.fan    = MS_FAN_AUTO;
    if (s.swingV >= MS_SWING_COUNT) s.swingV = MS_SWING_FIXED;
    if (s.swingH >= MS_SWING_COUNT) s.swingH = MS_SWING_FIXED;
    if (s.setpointF < MS_MIN_F)     s.setpointF = MS_MIN_F;
    if (s.setpointF > MS_MAX_F)     s.setpointF = MS_MAX_F;
}

void MINISPLIT_begin(void)
{
    defaults();

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true /* read-only */))
    {
        Serial.println("[MiniSplit] no saved state — using defaults");
        return;
    }

    StateBlob blob = {};
    size_t got = prefs.getBytes(NVS_KEY_STATE, &blob, sizeof(blob));
    prefs.end();

    if (got != sizeof(blob) || blob.version != BLOB_VERSION)
    {
        Serial.printf("[MiniSplit] saved state unusable (%u bytes, v%u) — using defaults\n",
                      (unsigned)got, blob.version);
        return;
    }

    s_state = blob.state;
    clampState(s_state);

    Serial.printf("[MiniSplit] restored: %s %s %d\xF8" "F fan %s\n",
                  s_state.power ? "ON" : "OFF", MINISPLIT_modeName(s_state.mode),
                  s_state.setpointF, MINISPLIT_fanName(s_state.fan));
}

static void save(void)
{
    StateBlob blob = {};
    blob.version = BLOB_VERSION;
    blob.state   = s_state;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
        return;
    prefs.putBytes(NVS_KEY_STATE, &blob, sizeof(blob));
    prefs.end();
}

MiniSplitState MINISPLIT_get(void) { return s_state; }
bool MINISPLIT_isLinked(void)      { return s_linked; }
bool MINISPLIT_isPending(void)     { return s_pending; }

const char* MINISPLIT_modeName(uint8_t mode)
{
    switch (mode)
    {
        case MS_MODE_HEAT: return "HEAT";
        case MS_MODE_COOL: return "COOL";
        default:           return "AUTO";
    }
}

const char* MINISPLIT_fanName(uint8_t fan)
{
    switch (fan)
    {
        case MS_FAN_LOW:  return "LOW";
        case MS_FAN_MED:  return "MED";
        case MS_FAN_HIGH: return "HIGH";
        default:          return "AUTO";
    }
}

// Mark the state changed; the frame goes out once the user stops adjusting.
static void touchState(void)
{
    s_pending   = true;
    s_changedMs = millis();
}

void MINISPLIT_setPower(bool on)
{
    if (s_state.power == on) return;
    s_state.power = on;
    touchState();
}

void MINISPLIT_setMode(uint8_t mode)
{
    if (mode >= MS_MODE_COUNT || s_state.mode == mode) return;
    s_state.mode = mode;
    touchState();
}

void MINISPLIT_setFan(uint8_t fan)
{
    if (fan >= MS_FAN_COUNT || s_state.fan == fan) return;
    s_state.fan = fan;
    touchState();
}

void MINISPLIT_setSwingV(uint8_t swing)
{
    if (swing >= MS_SWING_COUNT || s_state.swingV == swing) return;
    s_state.swingV = swing;
    touchState();
}

void MINISPLIT_setSwingH(uint8_t swing)
{
    if (swing >= MS_SWING_COUNT || s_state.swingH == swing) return;
    s_state.swingH = swing;
    touchState();
}

void MINISPLIT_adjustSetpoint(int16_t deltaF)
{
    int16_t want = (int16_t)(s_state.setpointF + deltaF);
    if (want < MS_MIN_F) want = MS_MIN_F;
    if (want > MS_MAX_F) want = MS_MAX_F;
    if (want == s_state.setpointF) return;

    s_state.setpointF = want;
    touchState();
}

// The whole command frame, as one unit. Becomes the ESP-NOW send to the IR
// blaster node; until that node exists it only reports what it would have sent.
static bool transmitState(const MiniSplitState& s)
{
    Serial.printf("[MiniSplit] TX %s mode %s set %d\xF8" "F fan %s vane V:%s H:%s\n",
                  s.power ? "ON" : "OFF",
                  MINISPLIT_modeName(s.mode), s.setpointF, MINISPLIT_fanName(s.fan),
                  (s.swingV == MS_SWING_ON) ? "swing" : "fixed",
                  (s.swingH == MS_SWING_ON) ? "swing" : "fixed");

    // TODO: esp_now_send() to the blaster node; set s_linked from the send
    // callback / the node's ack.
    return false;
}

void MINISPLIT_tick(void)
{
    if (s_pending)
    {
        if (millis() - s_changedMs < SETTLE_MS)
            return;                       // still adjusting

        s_pending = false;
        transmitState(s_state);
        s_sentMs = millis();
        save();                           // remember what was last commanded
        return;
    }

    // Idle refresh, so a missed frame does not leave the unit out of step for
    // long. Only once something has actually been commanded.
    if (s_sentMs != 0 && (millis() - s_sentMs) >= REFRESH_MS)
    {
        transmitState(s_state);
        s_sentMs = millis();
    }
}
