/*
===========================================================================
Name        : IRDump.ino
Author      : Brandon Van Pelt
Description : Identify the Della Vario (TL) mini-split's IR protocol.

              Based on IRremoteESP8266's IRrecvDumpV3 example with the OTA
              scaffolding stripped out.

              TWO BOARD FACTS THIS SKETCH DEPENDS ON
              1. The StickS3's Grove 5V rail is gated by the M5PM1 PMIC and is
                 OFF after a default M5Unified init -- which also kills the
                 onboard IR TX/RX. Nothing on the Grove port has power until
                 M5.Power.setExtOutput(true) is called. Skip that and the
                 receiver output simply floats.
              2. Pin mapping, from M5's two datasheets combined:
                    StickS3 Grove = GND / 5V / G9 (yellow) / G10 (white)
                    Unit IR       = yellow is IR_TX, white is IR_RX
                 so G9 drives the emitter and G10 reads the receiver.

              What we are looking for: the protocol name and bit count, and
              ideally a "Mesg Desc." line. If the library recognises the
              protocol as an A/C, it decodes the entire state (power, mode,
              temp, fan, swing) from one capture -- which both names the
              protocol and confirms the field mapping in one shot.

              Candidates for this unit (see project notes): Electra (which is
              how IRremoteESP8266 classifies AUX-OEM remotes), or the Tcl
              family -- TCL112AC / TCL96AC. A result of UNKNOWN is not a dead
              end; the rawData[] dump below is still replayable.

              NOTE: do NOT use the StickS3's onboard IR receiver (GPIO42) for
              this. M5 documents it as RMT-only, and this library's receive
              path is a hardware-timer ISR with no RMT support.

              Board: M5StickS3 (M5Stack board package).
              Needs: M5Unified (+ M5GFX), IRremoteESP8266.
===========================================================================
*/
#include <Arduino.h>
#include <M5Unified.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>

// Grove white wire -- the Unit IR's demodulated receiver output.
static const uint16_t IR_RX_PIN = 10;

// A/C remotes send long frames -- give the capture buffer plenty of room.
static const uint16_t kCaptureBufferSize = 1024;

// A/C protocols can leave 20-40ms gaps between the packets of a single
// message, so the idle timeout has to be longer than the usual 15ms or a
// frame gets chopped in half.
static const uint8_t kTimeout = 50;

// Ignore runts, so stray IR noise is not reported as an UNKNOWN message.
static const uint16_t kMinUnknownSize = 12;

IRrecv irrecv(IR_RX_PIN, kCaptureBufferSize, kTimeout, true);
decode_results results;

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    // THE line that makes the Grove port live. Without it the Unit IR has no
    // power at all and IR_RX_PIN just floats.
    M5.Power.setExtOutput(true);

    // Belt and braces: M5 documents the speaker amplifier as interfering with
    // IR reception. That note is about the ONBOARD receiver and we are using
    // the external Grove unit, but the speaker is dead weight here anyway.
    M5.Speaker.end();

    Serial.begin(115200);

    uint32_t start = millis();
    while (!Serial && millis() - start < 3000)
        delay(50);

    // Give the newly-powered Grove rail a moment to come up and the receiver
    // IC to settle before arming the capture.
    delay(200);

    Serial.println();
    Serial.printf("=== IR dump - listening on GPIO %u ===\n", IR_RX_PIN);
    Serial.println("Grove EXT_5V enabled via M5.Power.setExtOutput(true).");
    Serial.println("Point the Della remote at the IR unit and press a button.");
    Serial.println("Try POWER first, then a temperature change, then MODE.");
    Serial.println();

#if DECODE_HASH
    irrecv.setUnknownThreshold(kMinUnknownSize);
#endif
    irrecv.enableIRIn();
}

void loop()
{
    if (!irrecv.decode(&results))
        return;

    uint32_t now = millis();
    Serial.printf("Timestamp : %06u.%03u\n", now / 1000, now % 1000);

    if (results.overflow)
        Serial.printf("WARNING: code too big for the %u-entry buffer\n", kCaptureBufferSize);

    // Protocol name, bit count, value.
    Serial.print(resultToHumanReadableBasic(&results));

    // The payoff line, when the protocol is a recognised A/C.
    String description = IRAcUtils::resultAcToString(&results);
    if (description.length())
        Serial.println("Mesg Desc.: " + description);
    yield();

    // Replayable source form, including rawData[] for anything UNKNOWN.
    Serial.println(resultToSourceCode(&results));
    Serial.println();
    yield();
}
