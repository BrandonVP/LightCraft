/*
===========================================================================
Name        : RelayControl.cpp
Author      : Brandon Van Pelt
Description : Light relay hardware layer (see RelayControl.h).
===========================================================================
*/
#include "RelayControl.h"

#include <Preferences.h>
#include <string.h>

// GPIO per light — from the seller 86switch_onoff demo (doMain.cpp).
static const uint8_t LIGHT_PIN[LIGHT_COUNT] = { 40, 2, 1 };

// Active level: the demo drives HIGH = on, LOW = off.
static const bool LIGHT_ACTIVE_HIGH = true;

// Names are user-editable (Settings > General) and persist in NVS, so this is
// only what an unconfigured panel starts with.
static const char* const DEFAULT_NAME[LIGHT_COUNT] = { "Hall", "Room", "Fan" };

static const char*   NVS_NAMESPACE  = "lightcraft";
static const char*   NVS_KEY_NAMES  = "swnames";
static const uint8_t NAMES_VERSION  = 1;

struct NameBlob {
    uint8_t version;
    char    name[LIGHT_COUNT][RELAY_NAME_MAX];
};

static char s_name[LIGHT_COUNT][RELAY_NAME_MAX];
static bool s_on[LIGHT_COUNT];

static void copyName(uint8_t light, const char* src)
{
    strncpy(s_name[light], src, RELAY_NAME_MAX - 1);
    s_name[light][RELAY_NAME_MAX - 1] = '\0';
}

static void applyDefaultNames(void)
{
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        copyName(i, DEFAULT_NAME[i]);
}

static void loadNames(void)
{
    applyDefaultNames();

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true /* read-only */))
        return;

    NameBlob blob = {};
    size_t got = prefs.getBytes(NVS_KEY_NAMES, &blob, sizeof(blob));
    prefs.end();

    if (got != sizeof(blob) || blob.version != NAMES_VERSION)
        return;                       // nothing saved yet, or an older layout

    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
    {
        blob.name[i][RELAY_NAME_MAX - 1] = '\0';   // never trust stored bytes
        if (blob.name[i][0] != '\0')
            copyName(i, blob.name[i]);
    }

    Serial.printf("[Relay] names: 1=%s 2=%s 3=%s\n", s_name[0], s_name[1], s_name[2]);
}

static void saveNames(void)
{
    NameBlob blob = {};
    blob.version = NAMES_VERSION;
    for (uint8_t i = 0; i < LIGHT_COUNT; i++)
        strncpy(blob.name[i], s_name[i], RELAY_NAME_MAX - 1);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
    {
        Serial.println("[Relay] name save failed: cannot open NVS");
        return;
    }
    prefs.putBytes(NVS_KEY_NAMES, &blob, sizeof(blob));
    prefs.end();
}

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

    loadNames();
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
    return (light < LIGHT_COUNT) ? s_name[light] : "";
}

void RELAY_setName(uint8_t light, const char* name)
{
    if (light >= LIGHT_COUNT)
        return;

    char trimmed[RELAY_NAME_MAX] = { 0 };

    if (name != nullptr)
    {
        while (*name == ' ')          // leading spaces
            name++;

        strncpy(trimmed, name, RELAY_NAME_MAX - 1);
        trimmed[RELAY_NAME_MAX - 1] = '\0';

        for (int i = (int)strlen(trimmed) - 1; i >= 0 && trimmed[i] == ' '; i--)
            trimmed[i] = '\0';        // trailing spaces
    }

    // An empty entry means "put it back how it was" rather than leaving a
    // nameless switch that is hard to identify in Settings.
    const char* want = (trimmed[0] != '\0') ? trimmed : DEFAULT_NAME[light];

    if (strcmp(s_name[light], want) == 0)
        return;

    copyName(light, want);
    saveNames();

    Serial.printf("[Relay] switch %u renamed to \"%s\"\n",
                  (unsigned)(light + 1), s_name[light]);
}
