/*
===========================================================================
Name        : WiFiApp.h
Author      : Brandon Van Pelt
Description : Settings > WiFi — pick a network on the panel instead of
              compiling credentials in.

              Shows the current link, scans for networks, and on a tap hands
              off to the library's on-screen keyboard for the password. What
              you pick is saved by WiFiConfig and the network task re-associates
              with it.
===========================================================================
*/
#ifndef WIFIAPP_H
#define WIFIAPP_H

#include "appConfig.h"

uint8_t wifiApp_createBtns(void);
void    wifiApp_handler(int userInput);

// Call every loop(): collects async scan results and keeps the status line
// current. A no-op unless the page is showing.
void    wifiApp_tick(void);

#endif // WIFIAPP_H
