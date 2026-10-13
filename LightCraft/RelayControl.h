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

// Switch numbering is positional and fixed: LIGHT_1 is the LEFTMOST switch,
// both on screen and on the physical gang, through to LIGHT_3 on the right.
// Settings > General presents them as "Switch 1".."Switch 3" on that basis, so
// the number always identifies the same physical switch no matter how it has
// been renamed.
enum LightId {
    LIGHT_1 = 0,
    LIGHT_2,
    LIGHT_3,
    LIGHT_COUNT
};

// Longest switch name, terminator included.
//
// Ten characters is what the narrowest place a name is drawn can actually
// show: the Control tab's label is 125px wide and the built-in font renders
// 12px per character at text size 16, so 10 glyphs is 120px. Longer names are
// truncated at the point of drawing rather than clipped, which is the same
// convention the rest of the project uses.
#define RELAY_NAME_MAX 11

void RELAY_init();                    // GPIOs to outputs (all off) + load names
void RELAY_set(uint8_t light, bool on);
void RELAY_toggle(uint8_t light);
bool RELAY_isOn(uint8_t light);

// Display name for a switch, shared by every tab that labels one. Always
// returns a valid string; defaults are "Hall" / "Room" / "Fan".
const char* RELAY_name(uint8_t light);

// Rename a switch. Surrounding spaces are trimmed, the result is capped at
// RELAY_NAME_MAX - 1, and it is saved to NVS immediately. Clearing the name
// restores that switch's default rather than leaving a blank label.
void RELAY_setName(uint8_t light, const char* name);

#endif // RELAYCONTROL_H
