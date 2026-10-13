/*
===========================================================================
Name        : MiniSplit.cpp
Author      : Brandon Van Pelt
Description : Mini-split shadow state (see MiniSplit.h).
===========================================================================
*/
#include "MiniSplit.h"
#include "MsLink.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>

// MsLink.h restates this numbering because the node cannot include
// MiniSplit.h. If either side is ever renumbered, fail the build here rather
// than silently command the wrong mode.
static_assert((uint8_t)MS_MODE_HEAT    == (uint8_t)MSLINK_MODE_HEAT,    "MsLink mode drift");
static_assert((uint8_t)MS_MODE_COOL    == (uint8_t)MSLINK_MODE_COOL,    "MsLink mode drift");
static_assert((uint8_t)MS_MODE_AUTO    == (uint8_t)MSLINK_MODE_AUTO,    "MsLink mode drift");
static_assert((uint8_t)MS_MODE_DRY     == (uint8_t)MSLINK_MODE_DRY,     "MsLink mode drift");
static_assert((uint8_t)MS_MODE_FAN     == (uint8_t)MSLINK_MODE_FAN,     "MsLink mode drift");
static_assert((uint8_t)MS_FAN_AUTO     == (uint8_t)MSLINK_FAN_AUTO,     "MsLink fan drift");
static_assert((uint8_t)MS_FAN_LOW      == (uint8_t)MSLINK_FAN_LOW,      "MsLink fan drift");
static_assert((uint8_t)MS_FAN_MED      == (uint8_t)MSLINK_FAN_MED,      "MsLink fan drift");
static_assert((uint8_t)MS_FAN_HIGH     == (uint8_t)MSLINK_FAN_HIGH,     "MsLink fan drift");
static_assert((uint8_t)MS_SWING_FIXED  == (uint8_t)MSLINK_SWING_FIXED,  "MsLink swing drift");
static_assert((uint8_t)MS_SWING_ON     == (uint8_t)MSLINK_SWING_ON,     "MsLink swing drift");

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

// Periodic re-assert of the state over IR. DISABLED -- set non-zero to revive.
//
// This existed because IR is open loop and the unit may have missed a frame.
// In practice it does more harm than good: the panel cannot READ the unit, so
// "re-assert" really means "override whatever anyone else did". Every ten
// minutes the unit would beep and snap back to the panel's shadow, fighting
// the handheld remote and the Della app.
//
// What covers the original worry without overriding anyone:
//   - a command the NODE missed is retried by the next heartbeat, because the
//     node applies any sequence number it has not seen yet
//   - the node acks only once it has actually transmitted, so a node that
//     never sent anything shows up as "no link" instead of being silently
//     assumed good
//   - that leaves only "node transmitted but the AC did not hear it", which at
//     1-2m with line of sight is rare, self-evident (the panel disagrees with
//     the room) and fixed by one tap
static const uint32_t REFRESH_MS = 0;

static MiniSplitState s_state;
static bool           s_pending   = false;   // change waiting to be sent
static uint32_t       s_changedMs = 0;
static uint32_t       s_sentMs    = 0;

// --- Link to the IR blaster node (ESP-NOW broadcast; see MsLink.h) ----------
static bool     s_linkReady    = false;      // esp_now_init() has succeeded
static uint32_t s_linkTryMs    = 0;
static uint32_t s_seq          = 0;          // bumped only on a real command
static uint32_t s_lastHeardMs  = 0;          // last ACK or RESYNC from the node
static uint32_t s_pingMs       = 0;
static bool     s_everHeard    = false;

// Bounded command retry. The panel deliberately does NOT re-assert state on a
// timer (see MsLink.h), so a command that the node missed is covered here
// instead: repeat the same seq until it is acked, then stop. Repeats are
// idempotent because the node ignores a seq it has already applied.
static bool     s_awaitingAck  = false;
static uint32_t s_cmdSentMs    = 0;
static uint8_t  s_cmdTries     = 0;

// Bumped on every state change from any source. Pages watch it so a change
// made outside the UI (a resync from the handheld remote) actually repaints.
static uint32_t s_revision     = 0;

// The receive callback runs on the WiFi task, so it only parks the packet and
// MINISPLIT_tick() (UI loop) does the work -- the same discipline the rest of
// this project uses for anything touching shared state.
static volatile bool s_rxPending = false;
static MsLinkPacket  s_rxPacket;

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
bool MINISPLIT_isPending(void)     { return s_pending; }
uint32_t MINISPLIT_revision(void)  { return s_revision; }

// Linked means the node has answered recently, not merely that we once saw it.
// A node that lost power should show as "no link" on the panel rather than
// leaving the UI claiming commands are landing.
bool MINISPLIT_isLinked(void)
{
    return s_everHeard && (millis() - s_lastHeardMs < MSLINK_LINK_TIMEOUT_MS);
}

const char* MINISPLIT_modeName(uint8_t mode)
{
    switch (mode)
    {
        case MS_MODE_HEAT: return "HEAT";
        case MS_MODE_COOL: return "COOL";
        case MS_MODE_DRY:  return "DRY";
        case MS_MODE_FAN:  return "FAN";
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
    s_revision++;
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

uint8_t MINISPLIT_uiMode(void)
{
    return s_state.power ? (uint8_t)(s_state.mode + 1) : 0;
}

void MINISPLIT_setUiMode(uint8_t uiMode)
{
    if (uiMode == 0)
    {
        // Leaves s_state.mode alone, so turning the unit back on returns to the
        // mode it was running.
        MINISPLIT_setPower(false);
        return;
    }

    uint8_t mode = (uint8_t)(uiMode - 1);
    if (mode >= MS_MODE_COUNT)
        return;

    MINISPLIT_setMode(mode);
    MINISPLIT_setPower(true);
}

const char* MINISPLIT_uiModeName(uint8_t uiMode)
{
    return (uiMode == 0) ? "OFF" : MINISPLIT_modeName((uint8_t)(uiMode - 1));
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

// --- Wire conversion -------------------------------------------------------
// The two structs are deliberately separate: MiniSplitState is ours to change
// freely, MsLinkState is a wire format shared with another binary.
static MsLinkState toLink(const MiniSplitState& s)
{
    MsLinkState w;
    w.power     = s.power ? 1 : 0;
    w.mode      = s.mode;
    w.setpointF = s.setpointF;
    w.fan       = s.fan;
    w.swingV    = s.swingV;
    w.swingH    = s.swingH;
    return w;
}

static void fromLink(const MsLinkState& w, MiniSplitState& s)
{
    s.power     = (w.power != 0);
    s.mode      = w.mode;
    s.setpointF = w.setpointF;
    s.fan       = w.fan;
    s.swingV    = w.swingV;
    s.swingH    = w.swingH;
}

// --- ESP-NOW ---------------------------------------------------------------
static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    (void)info;
    if (!MSLINK_valid(data, len))
        return;

    const MsLinkPacket* p = (const MsLinkPacket*)data;
    if (p->type == MSLINK_CMD)      // our own broadcast coming back to us
        return;

    memcpy(&s_rxPacket, data, sizeof(MsLinkPacket));
    s_rxPending = true;
}

// Lazy, and retried: WiFi mode is set by the core-0 network task, so this may
// well be called before that task has run. Retrying from tick() avoids an
// ordering dependency between weather_begin() and this module.
static void linkEnsure(void)
{
    if (s_linkReady)
        return;

    if (s_linkTryMs != 0 && millis() - s_linkTryMs < 2000)
        return;
    s_linkTryMs = millis();

    if (WiFi.getMode() == WIFI_MODE_NULL)
        return;                     // network task has not started the radio yet

    if (esp_now_init() != ESP_OK)
        return;

    esp_now_register_recv_cb(onEspNowRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MSLINK_BROADCAST, 6);
    peer.channel = 0;               // follow whatever channel the STA is on
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK)
        return;

    s_linkReady = true;
    Serial.println("[MiniSplit] ESP-NOW up — broadcasting to the IR node");
}

static void broadcast(uint8_t type)
{
    if (!s_linkReady)
        return;

    MsLinkPacket p = {};
    p.magic   = MSLINK_MAGIC;
    p.version = MSLINK_VERSION;
    p.type    = type;
    p.flags   = 0;

    // A PING carries no state and no sequence, on purpose: there is then
    // nothing in a periodic packet that could ever command the unit, even if
    // the node's filtering were wrong.
    if (type != MSLINK_PING)
    {
        p.seq   = s_seq;
        p.state = toLink(s_state);
    }

    esp_now_send(MSLINK_BROADCAST, (const uint8_t*)&p, sizeof(p));
}

// The whole command frame, as one unit. Bumping the sequence is what tells the
// node this is a new command rather than a heartbeat, so it is the thing that
// actually causes an IR transmission at the other end.
static bool transmitState(const MiniSplitState& s)
{
    s_seq++;

    Serial.printf("[MiniSplit] TX#%lu %s mode %s set %d\xF8" "F fan %s vane V:%s H:%s\n",
                  (unsigned long)s_seq,
                  s.power ? "ON" : "OFF",
                  MINISPLIT_modeName(s.mode), s.setpointF, MINISPLIT_fanName(s.fan),
                  (s.swingV == MS_SWING_ON) ? "swing" : "fixed",
                  (s.swingH == MS_SWING_ON) ? "swing" : "fixed");

    broadcast(MSLINK_CMD);

    // Arm the bounded retry. Cleared by the node's ack for this seq.
    s_awaitingAck = true;
    s_cmdSentMs   = millis();
    s_cmdTries    = 1;

    return s_linkReady;
}

// A node packet: either it confirms our command reached the air, or it is
// telling us the handheld remote changed something behind our back.
static void processNodePacket(const MsLinkPacket& p)
{
    s_lastHeardMs = millis();
    s_everHeard   = true;

    if (p.type == MSLINK_ACK)
    {
        if (p.seq == s_seq)
            s_awaitingAck = false;        // delivered; stop retrying

        Serial.printf("[MiniSplit] node ack seq %lu%s\n", (unsigned long)p.seq,
                      (p.seq == s_seq) ? "" : " (stale)");
        return;
    }

    if (p.type != MSLINK_RESYNC)
        return;

    // Adopt what the node actually observed. Deliberately does NOT call
    // touchState(): the unit is already in this state, so commanding it again
    // would be pointless at best and a feedback loop at worst.
    MiniSplitState observed = s_state;
    fromLink(p.state, observed);
    clampState(observed);

    if (observed.power     == s_state.power
     && observed.mode      == s_state.mode
     && observed.setpointF == s_state.setpointF
     && observed.fan       == s_state.fan
     && observed.swingV    == s_state.swingV
     && observed.swingH    == s_state.swingH)
        return;                           // nothing actually differs

    s_state = observed;
    s_revision++;                         // so the open page repaints
    save();                               // the shadow moved; remember it

    Serial.printf("[MiniSplit] resync from remote: %s %s %d\xF8" "F fan %s\n",
                  s_state.power ? "ON" : "OFF", MINISPLIT_modeName(s_state.mode),
                  s_state.setpointF, MINISPLIT_fanName(s_state.fan));
}

void MINISPLIT_tick(void)
{
    linkEnsure();

    if (s_rxPending)
    {
        MsLinkPacket p;
        memcpy(&p, &s_rxPacket, sizeof(p));   // copy before clearing the flag
        s_rxPending = false;
        processNodePacket(p);
    }

    if (s_pending)
    {
        if (millis() - s_changedMs >= SETTLE_MS)
        {
            s_pending = false;
            transmitState(s_state);
            s_sentMs = millis();
            save();                       // remember what was last commanded
        }
    }
    else if (REFRESH_MS != 0 && s_sentMs != 0 && (millis() - s_sentMs) >= REFRESH_MS)
    {
        // Off by default -- see REFRESH_MS. Advances the sequence, so unlike
        // the heartbeat this really does make the node transmit again.
        transmitState(s_state);
        s_sentMs = millis();
    }

    // Bounded retry of an unacked command. Same seq, so the node ignores it if
    // it already applied it -- no risk of a second IR frame.
    if (s_awaitingAck
     && s_cmdTries < MSLINK_CMD_MAX_TRIES
     && (millis() - s_cmdSentMs) >= MSLINK_CMD_RETRY_MS)
    {
        s_cmdSentMs = millis();
        s_cmdTries++;
        Serial.printf("[MiniSplit] retry seq %lu (try %u)\n",
                      (unsigned long)s_seq, (unsigned)s_cmdTries);
        broadcast(MSLINK_CMD);
    }
    else if (s_awaitingAck && s_cmdTries >= MSLINK_CMD_MAX_TRIES)
    {
        s_awaitingAck = false;            // give up; the UI will show "no link"
        Serial.printf("[MiniSplit] seq %lu never acked — giving up\n",
                      (unsigned long)s_seq);
    }

    // Liveness + channel beacon. Carries no state, so it can never command the
    // unit -- which is the whole point. The node needs it to find and hold the
    // panel's radio channel.
    if (millis() - s_pingMs >= MSLINK_PING_MS)
    {
        s_pingMs = millis();
        broadcast(MSLINK_PING);
    }
}
