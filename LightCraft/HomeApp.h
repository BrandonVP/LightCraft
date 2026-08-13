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

#endif // HOMEAPP_H
