/*
===========================================================================
Name        : GeneralApp.h
Author      : Brandon Van Pelt
Description : Settings > General — a list of on/off preferences.

              The rows come from a table in the .cpp, so adding a preference is
              one entry there plus its getter/setter in GeneralSettings.*.
===========================================================================
*/
#ifndef GENERALAPP_H
#define GENERALAPP_H

#include "appConfig.h"

uint8_t general_createBtns(void);
void    general_handler(int userInput);

#endif // GENERALAPP_H
