/*
===========================================================================
Name        : ClimateApp.h
Author      : Brandon Van Pelt
Description : Full-screen mini-split page, opened by tapping the compact card
              on the Control tab.

              The Control tab keeps only what is used day to day — power and
              setpoint — and everything else lives here: mode, fan speed and
              both blade axes, with room left for whatever the IR protocol
              turns out to expose once the blaster node arrives.

              Registered under the Control menu, so the Control tab stays
              highlighted while it is showing and Back returns to it.
===========================================================================
*/
#ifndef CLIMATEAPP_H
#define CLIMATEAPP_H

#include "appConfig.h"

uint8_t climate_createBtns(void);
void    climate_handler(int userInput);

// Call every loop(): keeps the status line current while the page is showing.
void    climate_tick(void);

#endif // CLIMATEAPP_H
