/*
===========================================================================
Name        : TempRuleApp.h
Author      : Brandon Van Pelt
Description : Settings > Temp Rules — one row per light:

                  Fan   [ ABOVE ]   [ - ]  74°  [ + ]

              The mode button cycles OFF -> ABOVE -> BELOW, the -/+ buttons
              move the setpoint (hold to repeat), and SAVE writes the rules to
              flash. The rules themselves live in TempControl.*.
===========================================================================
*/
#ifndef TEMPRULEAPP_H
#define TEMPRULEAPP_H

#include "appConfig.h"

uint8_t temprule_createBtns(void);
void    temprule_handler(int userInput);

// Call every loop(): refreshes the live room-temperature readout and drives
// press-and-hold repeat on the -/+ buttons. No-op unless this page is showing.
void    temprule_tick(void);

#endif // TEMPRULEAPP_H
