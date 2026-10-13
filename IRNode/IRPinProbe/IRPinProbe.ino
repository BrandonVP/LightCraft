/*
===========================================================================
Name        : IRPinProbe.ino
Author      : Brandon Van Pelt
Description : Bring-up sanity check for the M5StickS3 + Grove Unit IR.

              The pin mapping is now known from M5's datasheets --
                 StickS3 Grove = GND / 5V / G9 (yellow) / G10 (white)
                 Unit IR       = yellow is IR_TX, white is IR_RX
              so G9 drives the emitter and G10 reads the receiver. This
              sketch is no longer needed to FIND the pin; it is kept as a
              quick "is the receiver alive and powered" check before blaming
              the protocol decoder for silence.

              Two corrections over the first version:
              1. The StickS3's Grove 5V rail is gated by the M5PM1 PMIC and is
                 OFF after a default M5Unified init. Without
                 M5.Power.setExtOutput(true) the Unit IR has no power and its
                 output floats -- which is what produced the original
                 "both pins idle=L, 100% low, zero edges" reading.
              2. INPUT_PULLUP rather than plain INPUT. On ESP32 a floating
                 high-impedance input tends to read 0, so plain INPUT cannot
                 tell "unconnected" from "driven low". With a pull-up:
                    floating        -> idle H, no edges, ever
                    live receiver   -> idle H, bursts of edges on a keypress
                    shorted/driven  -> stays L

              Board: M5StickS3 (M5Stack board package). Needs M5Unified.
===========================================================================
*/
#include <Arduino.h>
#include <M5Unified.h>

static const int PIN_TX = 9;     // Grove yellow -- Unit IR emitter drive
static const int PIN_RX = 10;    // Grove white  -- Unit IR receiver output

static const uint32_t WINDOW_MS = 1000;

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    // Makes the Grove port live. Everything downstream depends on this.
    M5.Power.setExtOutput(true);
    M5.Speaker.end();

    Serial.begin(115200);

    uint32_t start = millis();
    while (!Serial && millis() - start < 3000)
        delay(50);

    pinMode(PIN_TX, INPUT_PULLUP);
    pinMode(PIN_RX, INPUT_PULLUP);

    delay(200);   // let the Grove rail and the receiver IC settle

    Serial.println();
    Serial.println("=== IR pin probe - M5StickS3 Grove port ===");
    Serial.println("Grove EXT_5V enabled via M5.Power.setExtOutput(true).");
    Serial.println("Point the Della remote at the IR unit and hold a button.");
    Serial.println();
    Serial.println("G10 is the receiver: expect idle=H edges=0 when quiet,");
    Serial.println("then hundreds/thousands of edges while you hold a key.");
    Serial.println("If G10 stays at edges=0 forever, the module still has no");
    Serial.println("power or the cable is not seated.");
    Serial.println();
}

void loop()
{
    static uint32_t windowStart = 0;
    static uint32_t edgesTx = 0, edgesRx = 0;
    static uint32_t lowTx = 0, lowRx = 0, samples = 0;
    static int lastTx = -1, lastRx = -1;

    int tx = digitalRead(PIN_TX);
    int rx = digitalRead(PIN_RX);

    if (lastTx >= 0 && tx != lastTx) edgesTx++;
    if (lastRx >= 0 && rx != lastRx) edgesRx++;
    lastTx = tx;
    lastRx = rx;

    if (tx == LOW) lowTx++;
    if (rx == LOW) lowRx++;
    samples++;

    if (millis() - windowStart >= WINDOW_MS)
    {
        unsigned long pctTx = samples ? (lowTx * 100UL / samples) : 0UL;
        unsigned long pctRx = samples ? (lowRx * 100UL / samples) : 0UL;

        Serial.printf("G%-2d(TX) idle=%c edges=%6lu low=%3lu%%   |   G%-2d(RX) idle=%c edges=%6lu low=%3lu%%\n",
                      PIN_TX, tx ? 'H' : 'L', (unsigned long)edgesTx, pctTx,
                      PIN_RX, rx ? 'H' : 'L', (unsigned long)edgesRx, pctRx);

        edgesTx = edgesRx = lowTx = lowRx = samples = 0;
        windowStart = millis();
    }
}
