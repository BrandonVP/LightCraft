/*
===========================================================================
Name        : IRSendTest.ino
Author      : Brandon Van Pelt
Description : Prove we can actually command the Della Vario (TL) mini-split
              over IR, before any ESP-NOW plumbing is built on top.

              Protocol was identified by IRDump as TCL112AC, remote model
              TAC09CHSD, 112 bits / 14 state bytes.

              Drive it from the serial monitor:
                p   toggle power
                +   setpoint +1 F        -   setpoint -1 F
                m   cycle mode           f   cycle fan
                v   cycle vertical swing h   toggle horizontal swing
                s   re-send current state
                ?   print current state

              After every send this prints the 14 generated state bytes in
              the same format IRDump printed them, so you can compare our
              frame byte-for-byte against what the handheld remote sends for
              the same settings. They should match except for the checksum
              byte, which tracks the payload.

              BOARD NOTES
              - The Grove 5V rail is gated by the M5PM1 PMIC and is OFF after
                a default M5Unified init. M5.Power.setExtOutput(true) is what
                powers the Unit IR, the EMITTER included -- without it this
                sketch runs and transmits nothing.
              - Grove pin map: GND / 5V / G9 (yellow) / G10 (white), and the
                Unit IR assigns yellow = IR_TX, white = IR_RX. So we transmit
                on G9.

              TEMPERATURE: the protocol is Celsius, 16.0-31.0 in 0.5 steps,
              which is 60.8-87.8 F. The panel's MS_MIN_F of 60 is therefore
              one degree below what the unit can actually accept; 61 F is the
              real floor and this sketch clamps to it.

              Board: M5StickS3 (M5Stack board package).
              Needs: M5Unified (+ M5GFX), IRremoteESP8266.
===========================================================================
*/
#include <Arduino.h>
#include <M5Unified.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Tcl.h>
#include <math.h>

// Grove yellow wire -- the Unit IR's emitter drive.
static const uint16_t IR_TX_PIN = 9;

IRTcl112Ac ac(IR_TX_PIN);

// Test state, kept in Fahrenheit to match how the panel thinks.
struct TestState {
    bool     power  = true;
    uint8_t  mode   = kTcl112AcCool;
    int16_t  setF   = 72;
    uint8_t  fan    = kTcl112AcFanAuto;
    uint8_t  swingV = kTcl112AcSwingVOff;
    bool     swingH = false;
};

static TestState st;

// Protocol floor/ceiling expressed in F, derived from the Celsius limits.
static const int16_t TEST_MIN_F = 61;   // 16.0C = 60.8F, so 61 is the first reachable
static const int16_t TEST_MAX_F = 87;   // 31.0C = 87.8F

static float fToC(int16_t f)
{
    float c = (f - 32) * 5.0f / 9.0f;
    c = roundf(c * 2.0f) / 2.0f;              // the protocol takes 0.5 steps
    if (c < kTcl112AcTempMin) c = kTcl112AcTempMin;
    if (c > kTcl112AcTempMax) c = kTcl112AcTempMax;
    return c;
}

static const char* modeName(uint8_t m)
{
    switch (m)
    {
        case kTcl112AcHeat: return "HEAT";
        case kTcl112AcDry:  return "DRY";
        case kTcl112AcCool: return "COOL";
        case kTcl112AcFan:  return "FAN";
        case kTcl112AcAuto: return "AUTO";
        default:            return "?";
    }
}

static const char* fanName(uint8_t f)
{
    switch (f)
    {
        case kTcl112AcFanAuto: return "AUTO";
        case kTcl112AcFanMin:  return "QUIET";
        case kTcl112AcFanLow:  return "LOW";
        case kTcl112AcFanMed:  return "MED";
        case kTcl112AcFanHigh: return "HIGH";
        default:               return "?";
    }
}

static const char* swingVName(uint8_t s)
{
    switch (s)
    {
        case kTcl112AcSwingVOff:     return "OFF";
        case kTcl112AcSwingVHighest: return "HIGHEST";
        case kTcl112AcSwingVHigh:    return "HIGH";
        case kTcl112AcSwingVMiddle:  return "MIDDLE";
        case kTcl112AcSwingVLow:     return "LOW";
        case kTcl112AcSwingVLowest:  return "LOWEST";
        case kTcl112AcSwingVOn:      return "SWING";
        default:                     return "?";
    }
}

static void drawLcd(void)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 4);
    M5.Display.print("IR TX TEST  TCL112AC");

    M5.Display.setTextSize(3);
    M5.Display.setCursor(4, 24);
    M5.Display.setTextColor(st.power ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    M5.Display.print(st.power ? "ON " : "OFF");

    M5.Display.setTextSize(4);
    M5.Display.setCursor(4, 60);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.printf("%d", st.setF);
    M5.Display.setTextSize(2);
    M5.Display.print("F");

    M5.Display.setTextSize(2);
    M5.Display.setCursor(4, 110);
    M5.Display.print(modeName(st.mode));
    M5.Display.setCursor(4, 134);
    M5.Display.printf("FAN %s", fanName(st.fan));

    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 162);
    M5.Display.printf("V: %s", swingVName(st.swingV));
    M5.Display.setCursor(4, 176);
    M5.Display.printf("H: %s", st.swingH ? "SWING" : "OFF");
    M5.Display.setCursor(4, 198);
    M5.Display.printf("-> %.1fC", fToC(st.setF));
}

static void sendState(void)
{
    ac.setModel(tcl_ac_remote_model_t::TAC09CHSD);
    ac.setPower(st.power);
    ac.setMode(st.mode);
    ac.setTemp(fToC(st.setF));
    ac.setFan(st.fan);
    ac.setSwingVertical(st.swingV);
    ac.setSwingHorizontal(st.swingH);
    ac.setLight(true);          // the handheld remote's captures all had Light: On
    ac.send();

    Serial.printf("TX  %s  %s  %dF (%.1fC)  fan %s  V:%s  H:%s\n",
                  st.power ? "ON" : "OFF", modeName(st.mode),
                  st.setF, fToC(st.setF), fanName(st.fan),
                  swingVName(st.swingV), st.swingH ? "SWING" : "OFF");

    // Same shape as IRDump's output, for a direct byte-for-byte comparison
    // against what the handheld remote sent.
    uint8_t* raw = ac.getRaw();
    Serial.print("    state[14] = {");
    for (uint8_t i = 0; i < kTcl112AcStateLength; i++)
        Serial.printf("0x%02X%s", raw[i], (i + 1 < kTcl112AcStateLength) ? ", " : "");
    Serial.println("}");

    drawLcd();
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    // Powers the Grove port, the IR emitter included. Nothing works without it.
    M5.Power.setExtOutput(true);
    M5.Speaker.end();

    Serial.begin(115200);

    uint32_t start = millis();
    while (!Serial && millis() - start < 3000)
        delay(50);

    delay(200);                 // let the Grove rail settle
    ac.begin();

    M5.Display.setRotation(0);
    drawLcd();

    Serial.println();
    Serial.println("=== IR send test - TCL112AC on GPIO 9 ===");
    Serial.println("Point the Stick's IR unit at the indoor unit, 1-2m, line of sight.");
    Serial.println("Keys: p=power  +/-=temp  m=mode  f=fan  v=swingV  h=swingH  s=resend  ?=state");
    Serial.println();
    Serial.println("Sending initial state...");
    sendState();
}

void loop()
{
    M5.update();

    if (!Serial.available())
        return;

    char c = (char)Serial.read();
    bool send = true;

    switch (c)
    {
        case 'p':
            st.power = !st.power;
            break;

        case '+': case '=':
            if (st.setF < TEST_MAX_F) st.setF++;
            break;

        case '-': case '_':
            if (st.setF > TEST_MIN_F) st.setF--;
            break;

        case 'm':
            switch (st.mode)
            {
                case kTcl112AcCool: st.mode = kTcl112AcHeat; break;
                case kTcl112AcHeat: st.mode = kTcl112AcAuto; break;
                case kTcl112AcAuto: st.mode = kTcl112AcDry;  break;
                case kTcl112AcDry:  st.mode = kTcl112AcFan;  break;
                default:            st.mode = kTcl112AcCool; break;
            }
            break;

        case 'f':
            switch (st.fan)
            {
                case kTcl112AcFanAuto: st.fan = kTcl112AcFanLow;  break;
                case kTcl112AcFanLow:  st.fan = kTcl112AcFanMed;  break;
                case kTcl112AcFanMed:  st.fan = kTcl112AcFanHigh; break;
                case kTcl112AcFanHigh: st.fan = kTcl112AcFanMin;  break;
                default:               st.fan = kTcl112AcFanAuto; break;
            }
            break;

        case 'v':
            switch (st.swingV)
            {
                case kTcl112AcSwingVOff:     st.swingV = kTcl112AcSwingVOn;      break;
                case kTcl112AcSwingVOn:      st.swingV = kTcl112AcSwingVHighest; break;
                case kTcl112AcSwingVHighest: st.swingV = kTcl112AcSwingVHigh;    break;
                case kTcl112AcSwingVHigh:    st.swingV = kTcl112AcSwingVMiddle;  break;
                case kTcl112AcSwingVMiddle:  st.swingV = kTcl112AcSwingVLow;     break;
                case kTcl112AcSwingVLow:     st.swingV = kTcl112AcSwingVLowest;  break;
                default:                     st.swingV = kTcl112AcSwingVOff;     break;
            }
            break;

        case 'h':
            st.swingH = !st.swingH;
            break;

        case 's':
            break;              // just re-send

        case '?':
            send = false;
            Serial.printf("    %s  %s  %dF (%.1fC)  fan %s  V:%s  H:%s\n",
                          st.power ? "ON" : "OFF", modeName(st.mode),
                          st.setF, fToC(st.setF), fanName(st.fan),
                          swingVName(st.swingV), st.swingH ? "SWING" : "OFF");
            break;

        default:
            send = false;       // ignore newlines and anything unrecognised
            break;
    }

    if (send)
        sendState();
}
