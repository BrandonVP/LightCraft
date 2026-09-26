/*
===========================================================================
Name        : GeneralSettings.h
Author      : Brandon Van Pelt
Description : User preferences that shape the UI, saved to NVS.

              Not to be confused with appConfig.h, which is compile-time
              framework configuration. These are settings the user changes on
              the panel, from Settings > General.

              Stored as a bit field so adding a preference costs a flag rather
              than a new NVS key or a blob version bump.
===========================================================================
*/
#ifndef GENERALSETTINGS_H
#define GENERALSETTINGS_H

#include <Arduino.h>

// Load saved preferences. Call once in setup(), before the apps are registered.
void GSET_begin(void);

// Show the mini-split card on the Control tab. With it off, the light switches
// take the whole tab. Default on.
bool GSET_minisplitCard(void);
void GSET_setMinisplitCard(bool shown);

#endif // GENERALSETTINGS_H
