/*
===========================================================================
Name        : HistoryApp.h
Author      : Brandon Van Pelt
Description : Room temperature over the last 24 hours, opened by tapping the
              room line on the Home tab's weather card.

              Registered under the Home menu, so the Home tab stays highlighted
              while it is showing and Back returns to it.

              Like WeatherIcons this draws with Arduino_GFX directly — a plot
              needs lines and spans, which IDisplay does not carry. Call
              historyApp_begin() once with the same Arduino_GFX the adapter
              wraps.
===========================================================================
*/
#ifndef HISTORYAPP_H
#define HISTORYAPP_H

#include "appConfig.h"

class Arduino_GFX;

void    historyApp_begin(Arduino_GFX* gfx);

uint8_t historyApp_createBtns(void);
void    historyApp_handler(int userInput);

// Call every loop(): redraws when a new five-minute slot closes while the page
// is showing. A no-op elsewhere.
void    historyApp_tick(void);

#endif // HISTORYAPP_H
