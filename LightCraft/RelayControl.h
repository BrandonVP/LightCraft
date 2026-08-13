/*
===========================================================================
Name        : RelayControl.h
Author      : Brandon Van Pelt
Description : Hardware layer for the three light relays.

              Pins and polarity are taken from the seller's 86switch_onoff demo
              (doMain.cpp light1/2/3): GPIO 40, 2, 1, all active-HIGH, off at
              boot. The UI talks only to this interface.
===========================================================================
*/
#ifndef RELAYCONTROL_H
#define RELAYCONTROL_H

#include <Arduino.h>

enum LightId {
    LIGHT_1 = 0,
    LIGHT_2,
    LIGHT_3,
    LIGHT_COUNT
};

void RELAY_init();                    // set GPIOs to outputs, all off
void RELAY_set(uint8_t light, bool on);
void RELAY_toggle(uint8_t light);
bool RELAY_isOn(uint8_t light);

#endif // RELAYCONTROL_H
