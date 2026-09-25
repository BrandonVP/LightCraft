/*
===========================================================================
Name        : ForecastApp.h
Author      : Brandon Van Pelt
Description : 5-day forecast page, opened by tapping the Home tab's weather
              card. One row per day: name, drawn condition icon, high and low.

              Registered under the Home menu, so the Home tab stays highlighted
              while it is showing and the Back button returns to it. The data
              comes from WeatherTime's forecast getters.
===========================================================================
*/
#ifndef FORECASTAPP_H
#define FORECASTAPP_H

#include "appConfig.h"

uint8_t forecastApp_createBtns(void);
void    forecastApp_handler(int userInput);

// Call every loop(): rebuilds the page when a fresh forecast lands while it is
// showing. A no-op on every other page.
void    forecastApp_tick(void);

#endif // FORECASTAPP_H
