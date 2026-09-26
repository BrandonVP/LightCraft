/*
===========================================================================
Name        : DisplayApp.h
Author      : Brandon Van Pelt
Description : Settings > Display — backlight brightness, the idle level, and
              how long the panel waits before dimming. Drives Backlight.*.
===========================================================================
*/
#ifndef DISPLAYAPP_H
#define DISPLAYAPP_H

#include "appConfig.h"

uint8_t display_createBtns(void);
void    display_handler(int userInput);

#endif // DISPLAYAPP_H
