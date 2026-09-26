/*
===========================================================================
Name        : GeneralSettings.cpp
Author      : Brandon Van Pelt
Description : User preferences (see GeneralSettings.h).
===========================================================================
*/
#include "GeneralSettings.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "lightcraft";
static const char* NVS_KEY_FLAGS = "genflags";

// One bit per preference. A new setting is a new flag, and an older saved value
// simply has that bit clear — so DEFAULT_FLAGS decides what "unset" means and
// the stored value needs no version.
static const uint32_t FLAG_MINISPLIT_CARD = 0x00000001;
static const uint32_t DEFAULT_FLAGS = FLAG_MINISPLIT_CARD;

static uint32_t s_flags = DEFAULT_FLAGS;

static void save(void)
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
    {
        Serial.println("[GenSet] save failed: cannot open NVS");
        return;
    }
    prefs.putUInt(NVS_KEY_FLAGS, s_flags);
    prefs.end();
}

void GSET_begin(void)
{
    s_flags = DEFAULT_FLAGS;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true /* read-only */))
    {
        Serial.println("[GenSet] no saved preferences - using defaults");
        return;
    }
    s_flags = prefs.getUInt(NVS_KEY_FLAGS, DEFAULT_FLAGS);
    prefs.end();

    Serial.printf("[GenSet] flags 0x%08lX (mini-split card %s)\n",
                  (unsigned long)s_flags, GSET_minisplitCard() ? "on" : "off");
}

bool GSET_minisplitCard(void)
{
    return (s_flags & FLAG_MINISPLIT_CARD) != 0;
}

void GSET_setMinisplitCard(bool shown)
{
    const uint32_t want = shown ? (s_flags | FLAG_MINISPLIT_CARD)
                                : (s_flags & ~FLAG_MINISPLIT_CARD);
    if (want == s_flags)
        return;

    s_flags = want;
    save();
}
