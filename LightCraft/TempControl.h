/*
===========================================================================
Name        : TempControl.h
Author      : Brandon Van Pelt
Description : Room-temperature automation for the three light relays.

              Each light carries one rule: a mode (off / on-above / on-below)
              and a setpoint in degrees F. TEMPCTL_tick() compares the room
              reading published by WeatherTime against every enabled rule and
              switches that light's relay when the setpoint is crossed.

              The rules are EDGE triggered: a light is switched only on the
              crossing, never held. A manual tap on the Switches tab therefore
              always wins until the temperature next crosses the setpoint, so
              the automation and the user never fight over a relay.

              An edited rule takes effect as soon as the edit settles; SAVE only
              makes it survive a reboot.

              Rules persist in the ESP32's NVS flash (Preferences), not on the
              SD card: the TF slot shares its SPI bus (IO47/IO48) with the
              ST7701 panel's command lines, and flash needs no card present.
===========================================================================
*/
#ifndef TEMPCONTROL_H
#define TEMPCONTROL_H

#include <Arduino.h>
#include "RelayControl.h"

enum TempMode {
    TEMP_MODE_OFF = 0,   // no automation for this light
    TEMP_MODE_ABOVE,     // turn on when the room is at/above the setpoint
    TEMP_MODE_BELOW,     // turn on when the room is at/below the setpoint
    TEMP_MODE_COUNT
};

struct TempRule {
    uint8_t mode;        // one of TempMode
    int16_t setpointF;
};

// Setpoint limits + step used by the settings UI.
static const int16_t TEMPCTL_MIN_F  = 40;
static const int16_t TEMPCTL_MAX_F  = 95;
static const int16_t TEMPCTL_STEP_F = 1;

// Deadband, in degrees F, applied on the release side of a rule so a reading
// hovering on the setpoint cannot chatter the relay.
static const int16_t TEMPCTL_HYSTERESIS_F = 2;

// Load the saved rules from flash. Call once in setup(), after RELAY_init().
void TEMPCTL_begin(void);

// Persist the current rules to flash. Returns true when the write succeeded.
bool TEMPCTL_save(void);

// Evaluate the rules against the latest room reading. Call every loop();
// internally rate-limited, and a no-op while the room reading is missing or
// stale, so a dead sensor leaves the relays exactly as the user left them.
void TEMPCTL_tick(void);

TempRule TEMPCTL_get(uint8_t light);
void     TEMPCTL_setMode(uint8_t light, uint8_t mode);
void     TEMPCTL_setSetpoint(uint8_t light, int16_t setpointF);

// true when the in-memory rules differ from what is stored in flash.
bool TEMPCTL_isDirty(void);

// "OFF" / "ABOVE" / "BELOW" — for the settings UI.
const char* TEMPCTL_modeName(uint8_t mode);

#endif // TEMPCONTROL_H
