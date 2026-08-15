/*
===========================================================================
Name        : HomeApp.h
Author      : Brandon Van Pelt
Description : Home tab — date, time and weather. Placeholder content for now;
              time (NTP) and weather (API) are a future feature.
===========================================================================
*/
#ifndef HOMEAPP_H
#define HOMEAPP_H

#include "appConfig.h"

uint8_t home_createBtns(void);
void    home_handler(int userInput);

// Seed the RTC from the build timestamp so the clock runs before NTP is wired
// up. Call once in setup(). NTP (a future feature) just re-syncs the same clock.
void    home_seedClockFromBuild(void);

// Call every loop(): refreshes the on-screen time/date once per second while the
// Home tab is showing. A no-op on other tabs.
void    home_tick(void);

#endif // HOMEAPP_H
