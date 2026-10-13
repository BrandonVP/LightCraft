/*
===========================================================================
Name        : MiniSplitNode.ino
Author      : Brandon Van Pelt
Description : IR blaster node for the room's Della Vario (TL) mini-split.
              Runs on an M5StickS3 with a Grove "Unit IR" attached, parked
              near the indoor unit with line of sight to its receiver window.

              Takes commands from the LightCraft panel over ESP-NOW, sends
              them to the AC as TCL112AC infrared, and -- the part that makes
              the whole open-loop problem tolerable -- also LISTENS. When
              someone uses the handheld remote, the node decodes that frame
              and pushes the real state back to the panel, so the panel's
              shadow re-converges instead of quietly lying.

              PROTOCOL: TCL112AC, remote model TAC09CHSD, 112 bits / 14 bytes.
              Identified live off the handheld remote (see IRDump).

              PINS: Grove is GND / 5V / G9 (yellow) / G10 (white), and the
              Unit IR assigns yellow = IR_TX, white = IR_RX. So G9 transmits
              and G10 receives.

              THE GROVE RAIL IS OFF BY DEFAULT. It is gated by the M5PM1 PMIC
              and a default M5Unified init leaves it disabled, which also
              kills the onboard IR. M5.Power.setExtOutput(true) is what makes
              any of this work.

              CHANNEL ACQUISITION: ESP-NOW peers must share a radio channel,
              and the panel's channel is whatever its router picked. Rather
              than hard-code it or teach the node the WiFi credentials, the
              node hops channels until it hears a panel heartbeat, then locks
              on. Survives the router changing channel too -- it just loses
              the link briefly and re-acquires.

              STANDALONE TEST: the two side buttons drive the AC directly, so
              the node is testable before the panel half exists.
                 BtnA  toggle power
                 BtnB  setpoint +1 F (wraps at the top back to the minimum)

              Board: M5StickS3 (M5Stack board package).
              Needs: M5Unified (+ M5GFX), IRremoteESP8266.
===========================================================================
*/
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>
#include <ir_Tcl.h>

#include <math.h>
#include "MsLink.h"

// --- Hardware --------------------------------------------------------------
static const uint16_t IR_TX_PIN = 9;    // Grove yellow
static const uint16_t IR_RX_PIN = 10;   // Grove white

// A/C frames are long and can have 20-40ms inter-packet gaps.
static const uint16_t kCaptureBufferSize = 1024;
static const uint8_t  kTimeout           = 50;
static const uint16_t kMinUnknownSize    = 12;

IRTcl112Ac ac(IR_TX_PIN);
IRrecv     irrecv(IR_RX_PIN, kCaptureBufferSize, kTimeout, true);

// Parses received frames only -- constructed on the TX pin but begin() is
// never called on it, so it never drives the pin. Keeping it separate from
// `ac` means an incoming frame cannot clobber our outgoing state.
IRTcl112Ac parser(IR_TX_PIN);

decode_results results;

// --- Protocol limits -------------------------------------------------------
// TCL112AC is Celsius 16.0-31.0 in 0.5 steps = 60.8-87.8F, so 61F is the
// lowest setpoint the unit will actually accept.
static const int16_t NODE_MIN_F = 61;
static const int16_t NODE_MAX_F = 87;

// --- Link state ------------------------------------------------------------
static MsLinkState s_state;                   // what we believe the AC is set to
static uint32_t    s_lastSeq      = 0;        // last command seq applied
static bool        s_haveSeq      = false;
static uint32_t    s_lastHeardMs  = 0;        // last valid panel packet
static bool        s_channelLocked = false;
static uint8_t     s_channel      = 1;
static uint32_t    s_lastHopMs    = 0;
static uint32_t    s_txCount      = 0;
static uint32_t    s_resyncCount  = 0;

// Our own transmission reflects off the room and can land back on the
// receiver. RX is disabled across the send, but ignore decodes for a moment
// afterwards so a late echo is never mistaken for the handheld remote.
static uint32_t    s_lastTxMs     = 0;
static const uint32_t ECHO_BLANK_MS = 600;

// The ESP-NOW receive callback runs on the WiFi task, where a ~100ms IR
// transmission would be badly out of place. The callback only parks the
// packet; loop() does the work.
static volatile bool s_rxPending = false;
static MsLinkPacket  s_rxPacket;

static const uint32_t HOP_DWELL_MS = 250;
static const uint8_t  MAX_CHANNEL  = 13;

// --- Conversions -----------------------------------------------------------
static float fToC(int16_t f)
{
    float c = (f - 32) * 5.0f / 9.0f;
    c = roundf(c * 2.0f) / 2.0f;                      // protocol takes 0.5 steps
    if (c < kTcl112AcTempMin) c = kTcl112AcTempMin;
    if (c > kTcl112AcTempMax) c = kTcl112AcTempMax;
    return c;
}

static int16_t cToF(float c)
{
    int16_t f = (int16_t)lroundf(c * 9.0f / 5.0f + 32.0f);
    if (f < NODE_MIN_F) f = NODE_MIN_F;
    if (f > NODE_MAX_F) f = NODE_MAX_F;
    return f;
}

static uint8_t modeToTcl(uint8_t m)
{
    switch (m)
    {
        case MSLINK_MODE_HEAT: return kTcl112AcHeat;
        case MSLINK_MODE_AUTO: return kTcl112AcAuto;
        case MSLINK_MODE_DRY:  return kTcl112AcDry;
        case MSLINK_MODE_FAN:  return kTcl112AcFan;
        default:               return kTcl112AcCool;
    }
}

static uint8_t fanToTcl(uint8_t f)
{
    switch (f)
    {
        case MSLINK_FAN_LOW:  return kTcl112AcFanLow;
        case MSLINK_FAN_MED:  return kTcl112AcFanMed;
        case MSLINK_FAN_HIGH: return kTcl112AcFanHigh;
        default:              return kTcl112AcFanAuto;
    }
}

// Reverse maps, for turning a decoded handheld-remote frame back into our
// vocabulary. stdAc is the library's protocol-neutral AC state.
static uint8_t tclModeToLink(stdAc::opmode_t m)
{
    switch (m)
    {
        case stdAc::opmode_t::kHeat: return MSLINK_MODE_HEAT;
        case stdAc::opmode_t::kAuto: return MSLINK_MODE_AUTO;
        case stdAc::opmode_t::kCool: return MSLINK_MODE_COOL;
        case stdAc::opmode_t::kDry:  return MSLINK_MODE_DRY;
        case stdAc::opmode_t::kFan:  return MSLINK_MODE_FAN;
        default:                     return MSLINK_MODE_COOL;
    }
}

static uint8_t tclFanToLink(stdAc::fanspeed_t f)
{
    switch (f)
    {
        case stdAc::fanspeed_t::kMin:
        case stdAc::fanspeed_t::kLow:    return MSLINK_FAN_LOW;
        case stdAc::fanspeed_t::kMedium: return MSLINK_FAN_MED;
        case stdAc::fanspeed_t::kHigh:
        case stdAc::fanspeed_t::kMax:    return MSLINK_FAN_HIGH;
        default:                         return MSLINK_FAN_AUTO;
    }
}

static const char* modeName(uint8_t m)
{
    switch (m)
    {
        case MSLINK_MODE_HEAT: return "HEAT";
        case MSLINK_MODE_AUTO: return "AUTO";
        case MSLINK_MODE_DRY:  return "DRY";
        case MSLINK_MODE_FAN:  return "FAN";
        default:               return "COOL";
    }
}

static const char* fanName(uint8_t f)
{
    switch (f)
    {
        case MSLINK_FAN_LOW:  return "LOW";
        case MSLINK_FAN_MED:  return "MED";
        case MSLINK_FAN_HIGH: return "HIGH";
        default:              return "AUTO";
    }
}

// --- Display ---------------------------------------------------------------
static void drawLcd(void)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);

    bool linked = s_channelLocked &&
                  (millis() - s_lastHeardMs < MSLINK_LINK_TIMEOUT_MS);

    M5.Display.setCursor(4, 4);
    M5.Display.setTextColor(linked ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    if (linked)                     M5.Display.printf("LINK ch%u", s_channel);
    else if (s_channelLocked)       M5.Display.printf("LOST ch%u", s_channel);
    else                            M5.Display.printf("SCAN ch%u", s_channel);

    M5.Display.setTextSize(3);
    M5.Display.setCursor(4, 24);
    M5.Display.setTextColor(s_state.power ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    M5.Display.print(s_state.power ? "ON " : "OFF");

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(4);
    M5.Display.setCursor(4, 60);
    M5.Display.printf("%d", s_state.setpointF);
    M5.Display.setTextSize(2);
    M5.Display.print("F");

    M5.Display.setTextSize(2);
    M5.Display.setCursor(4, 110);
    M5.Display.print(modeName(s_state.mode));
    M5.Display.setCursor(4, 134);
    M5.Display.printf("FAN %s", fanName(s_state.fan));

    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 162);
    M5.Display.printf("V:%s H:%s",
                      s_state.swingV == MSLINK_SWING_ON ? "SWING" : "FIX",
                      s_state.swingH == MSLINK_SWING_ON ? "SWING" : "FIX");
    M5.Display.setCursor(4, 180);
    M5.Display.printf("tx %lu  resync %lu", (unsigned long)s_txCount,
                      (unsigned long)s_resyncCount);
    M5.Display.setCursor(4, 198);
    M5.Display.printf("seq %lu", (unsigned long)s_lastSeq);
}

// --- IR --------------------------------------------------------------------
static void sendIr(void)
{
    // Receiving while transmitting on the same module is asking for trouble;
    // the capture ISR and the bit-banged carrier do not mix.
    irrecv.disableIRIn();

    ac.setModel(tcl_ac_remote_model_t::TAC09CHSD);
    ac.setPower(s_state.power != 0);
    ac.setMode(modeToTcl(s_state.mode));
    ac.setTemp(fToC(s_state.setpointF));
    ac.setFan(fanToTcl(s_state.fan));
    ac.setSwingVertical(s_state.swingV == MSLINK_SWING_ON ? kTcl112AcSwingVOn
                                                          : kTcl112AcSwingVOff);
    ac.setSwingHorizontal(s_state.swingH == MSLINK_SWING_ON);
    ac.setLight(true);                 // matches what the handheld remote sends
    ac.send();

    s_lastTxMs = millis();
    s_txCount++;

    irrecv.enableIRIn();

    Serial.printf("[node] IR TX  %s %s %dF fan %s V:%s H:%s\n",
                  s_state.power ? "ON" : "OFF", modeName(s_state.mode),
                  s_state.setpointF, fanName(s_state.fan),
                  s_state.swingV == MSLINK_SWING_ON ? "swing" : "fixed",
                  s_state.swingH == MSLINK_SWING_ON ? "swing" : "fixed");
}

// --- ESP-NOW ---------------------------------------------------------------
static void sendPacket(uint8_t type, uint32_t seq)
{
    MsLinkPacket p = {};
    p.magic   = MSLINK_MAGIC;
    p.version = MSLINK_VERSION;
    p.type    = type;
    p.flags   = 0;
    p.seq     = seq;
    p.state   = s_state;

    esp_now_send(MSLINK_BROADCAST, (const uint8_t*)&p, sizeof(p));
}

static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    (void)info;
    if (!MSLINK_valid(data, len))
        return;

    // Only the panel's two outbound types interest us; our own ACK and RESYNC
    // broadcasts come back to us and must be ignored.
    const MsLinkPacket* p = (const MsLinkPacket*)data;
    if (p->type != MSLINK_CMD && p->type != MSLINK_PING)
        return;

    // Park it; loop() does the real work off the WiFi task.
    memcpy(&s_rxPacket, data, sizeof(MsLinkPacket));
    s_rxPending = true;
}

static bool espNowBegin(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();              // ESP-NOW only; never associates

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("[node] esp_now_init FAILED");
        return false;
    }

    esp_now_register_recv_cb(onEspNowRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MSLINK_BROADCAST, 6);
    peer.channel = 0;               // 0 = whatever channel we are currently on
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK)
    {
        Serial.println("[node] esp_now_add_peer(broadcast) FAILED");
        return false;
    }

    return true;
}

static void hopChannel(void)
{
    if (s_channelLocked || millis() - s_lastHopMs < HOP_DWELL_MS)
        return;

    s_channel = (uint8_t)((s_channel % MAX_CHANNEL) + 1);
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
    s_lastHopMs = millis();
}

// --- Command handling ------------------------------------------------------
// Any valid panel packet proves the link and pins the channel.
static void noteHeard(void)
{
    s_lastHeardMs = millis();

    if (!s_channelLocked)
    {
        s_channelLocked = true;
        Serial.printf("[node] panel found on channel %u — locked\n", s_channel);
    }
}

static void handleCommand(const MsLinkPacket& p)
{
    noteHeard();

    // Only a real change carries a new seq. Retries repeat the same seq and
    // land here as "already applied", so they cost nothing at the AC.
    bool isNew = !s_haveSeq || (p.seq != s_lastSeq);
    if (!isNew)
        return;

    s_state   = p.state;
    s_lastSeq = p.seq;
    s_haveSeq = true;

    if (s_state.setpointF < NODE_MIN_F) s_state.setpointF = NODE_MIN_F;
    if (s_state.setpointF > NODE_MAX_F) s_state.setpointF = NODE_MAX_F;

    sendIr();
    sendPacket(MSLINK_ACK, s_lastSeq);      // reports that IR actually went out
    drawLcd();
}

// --- Handheld-remote resync ------------------------------------------------
static void pollIr(void)
{
    if (!irrecv.decode(&results))
        return;

    if (results.decode_type != decode_type_t::TCL112AC)
        return;

    // The remote interleaves a short "Type 2" frame (byte 3 == 0x02) with the
    // full-state Type 1 frame. Only Type 1 describes the unit's settings.
    if (results.bits != 112 || results.state[3] != kTcl112AcNormal)
        return;

    // Our own reflected transmission is not news.
    if (millis() - s_lastTxMs < ECHO_BLANK_MS)
        return;

    stdAc::state_t decoded;
    if (!IRAcUtils::decodeToState(&results, &decoded))
        return;

    MsLinkState observed;
    observed.power     = decoded.power ? 1 : 0;
    observed.mode      = tclModeToLink(decoded.mode);
    observed.setpointF = cToF(decoded.celsius ? decoded.degrees
                                              : (decoded.degrees - 32) * 5.0f / 9.0f);
    observed.fan       = tclFanToLink(decoded.fanspeed);
    observed.swingV    = (decoded.swingv == stdAc::swingv_t::kOff) ? MSLINK_SWING_FIXED
                                                                   : MSLINK_SWING_ON;
    observed.swingH    = (decoded.swingh == stdAc::swingh_t::kOff) ? MSLINK_SWING_FIXED
                                                                   : MSLINK_SWING_ON;

    if (MSLINK_sameState(observed, s_state))
        return;                             // remote agreed with us; nothing to do

    s_state = observed;
    s_resyncCount++;

    Serial.printf("[node] REMOTE  %s %s %dF fan %s  -> pushing resync\n",
                  s_state.power ? "ON" : "OFF", modeName(s_state.mode),
                  s_state.setpointF, fanName(s_state.fan));

    sendPacket(MSLINK_RESYNC, s_lastSeq);
    drawLcd();
}

// --- Buttons (standalone testing) ------------------------------------------
static void pollButtons(void)
{
    if (M5.BtnA.wasPressed())
    {
        s_state.power = !s_state.power;
        sendIr();
        sendPacket(MSLINK_RESYNC, s_lastSeq);   // tell the panel what we just did
        drawLcd();
    }

    if (M5.BtnB.wasPressed())
    {
        s_state.setpointF++;
        if (s_state.setpointF > NODE_MAX_F)
            s_state.setpointF = NODE_MIN_F;
        sendIr();
        sendPacket(MSLINK_RESYNC, s_lastSeq);
        drawLcd();
    }
}

// --- Setup / loop ----------------------------------------------------------
void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    // Powers the Grove port -- the IR emitter and receiver both. Without this
    // the node runs perfectly and controls nothing.
    M5.Power.setExtOutput(true);
    M5.Speaker.end();

    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && millis() - start < 3000)
        delay(50);

    delay(200);                     // let the Grove rail settle

    // A sane starting shadow. The first panel command overwrites it, and a
    // handheld-remote press corrects it.
    s_state.power     = 0;
    s_state.mode      = MSLINK_MODE_COOL;
    s_state.setpointF = 72;
    s_state.fan       = MSLINK_FAN_AUTO;
    s_state.swingV    = MSLINK_SWING_FIXED;
    s_state.swingH    = MSLINK_SWING_FIXED;

    ac.begin();
#if DECODE_HASH
    irrecv.setUnknownThreshold(kMinUnknownSize);
#endif
    irrecv.enableIRIn();

    Serial.println();
    Serial.println("=== MiniSplit IR node (TCL112AC / TAC09CHSD) ===");
    Serial.printf("IR TX G%u, IR RX G%u\n", IR_TX_PIN, IR_RX_PIN);

    if (espNowBegin())
        Serial.println("ESP-NOW up; hopping channels to find the panel...");

    M5.Display.setRotation(0);
    drawLcd();
}

void loop()
{
    M5.update();

    if (s_rxPending)
    {
        MsLinkPacket p;
        memcpy(&p, &s_rxPacket, sizeof(p));   // copy before clearing the flag
        s_rxPending = false;

        if (p.type == MSLINK_CMD)
            handleCommand(p);
        else
            noteHeard();                      // MSLINK_PING: liveness only
    }

    pollIr();
    pollButtons();
    hopChannel();

    // Drop the lock if the panel goes quiet, so a router channel change is
    // recovered from by resuming the scan rather than sitting deaf forever.
    if (s_channelLocked && millis() - s_lastHeardMs > MSLINK_LINK_TIMEOUT_MS * 2)
    {
        Serial.println("[node] panel silent — resuming channel scan");
        s_channelLocked = false;
        drawLcd();
    }

    // Keep the header's LINK/LOST indicator honest without redrawing
    // constantly.
    static uint32_t lastRedraw = 0;
    if (millis() - lastRedraw > 1000)
    {
        lastRedraw = millis();
        drawLcd();
    }
}
