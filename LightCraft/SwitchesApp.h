/*
===========================================================================
Name        : SwitchesApp.h
Author      : Brandon Van Pelt
Description : Switches tab — three on/off light toggles in a horizontal row.
              Turning a light on arms a 30s timer that returns to the Home tab.
===========================================================================
*/
#ifndef SWITCHESAPP_H
#define SWITCHESAPP_H

#include "appConfig.h"

uint8_t switches_createBtns(void);
void    switches_handler(int userInput);

// Call every loop(): after a light is turned on, returns to the Home tab once
// the auto-return delay elapses. Runs regardless of the active tab.
void    switches_tick(void);

#endif // SWITCHESAPP_H
