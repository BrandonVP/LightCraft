/*
===========================================================================
Name        : RelayControl.cpp
Author      : Brandon Van Pelt
Description : Light relay hardware layer (see RelayControl.h).
===========================================================================
*/
#include "RelayControl.h"

// GPIO per light — from the seller 86switch_onoff demo (doMain.cpp).
static const uint8_t LIGHT_PIN[LIGHT_COUNT] = { 40, 2, 1 };

// Active level: the demo drives HIGH = on, LOW = off.
static const bool LIGHT_ACTIVE_HIGH = true;

// Display names, shared by the Switches tab and the Temp Rules settings app.
static const char* const LIGHT_NAME[LIGHT_COUNT] = { "Hall", "Room", "Fan" };

static bool s_on[LIGHT_COUNT];

static void drive(uint8_t light, bool on)
{
    digitalWrite(LIGHT_PIN[light], (on == LIGHT_ACTIVE_HIGH) ? HIGH : LOW);
}

void RELAY_init()
{
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        s_on[i] = false;
        pinMode(LIGHT_PIN[i], OUTPUT);
        drive(i, false);
    }
}

void RELAY_set(uint8_t light, bool on)
{
    if (light >= LIGHT_COUNT) return;
    s_on[light] = on;
    drive(light, on);
}

void RELAY_toggle(uint8_t light)
{
    if (light < LIGHT_COUNT) RELAY_set(light, !s_on[light]);
}

bool RELAY_isOn(uint8_t light)
{
    return light < LIGHT_COUNT && s_on[light];
}

const char* RELAY_name(uint8_t light)
{
    return (light < LIGHT_COUNT) ? LIGHT_NAME[light] : "";
}
