/*
===========================================================================
Name        : ControlApp.h
Author      : Brandon Van Pelt
Description : Control tab — room mini-split on top, light switches along the
              bottom. Replaces the old Switches tab.

              The mini-split card carries only power and setpoint, and the
              whole card is a button that opens the full page (ClimateApp) for
              mode, fan and blades. Climate widgets drive MiniSplit.*; the light
              row drives RelayControl.* and still shows any temperature rule set
              in Settings > Temp Rules.

              Turning a light on arms a 30s timer that returns to the Home tab;
              any further tap on this page pushes that out, so adjusting the
              mini-split does not get interrupted mid-edit.
===========================================================================
*/
#ifndef CONTROLAPP_H
#define CONTROLAPP_H

#include "appConfig.h"

uint8_t control_createBtns(void);
void    control_handler(int userInput);

// Call every loop(): runs the return-to-Home timer, repaints a light the
// temperature rules switched, and refreshes the mini-split status line.
void    control_tick(void);

#endif // CONTROLAPP_H
