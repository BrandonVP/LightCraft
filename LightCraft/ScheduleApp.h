/*
===========================================================================
Name        : ScheduleApp.h
Author      : Brandon Van Pelt
Description : Settings > Schedules — a list of the three lights with their
              current schedule, and behind it an editor for one light.

              Two pages, one file, because they share the layout vocabulary.
              The editor is registered on the hidden menu so it never shows up
              in the Settings list; it is reached by tapping a row and leaves
              by its Back button.

              Edits save themselves once they settle, so there is no save
              button to forget.
===========================================================================
*/
#ifndef SCHEDULEAPP_H
#define SCHEDULEAPP_H

#include "appConfig.h"

// Settings > Schedules (the list)
uint8_t schedule_createBtns(void);
void    schedule_handler(int userInput);

// The per-light editor behind it
uint8_t schedEdit_createBtns(void);
void    schedEdit_handler(int userInput);

#endif // SCHEDULEAPP_H
