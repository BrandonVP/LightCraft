/*
===========================================================================
Name        : Schedule.h
Author      : Brandon Van Pelt
Description : Time-of-day schedules for the three light relays.

              Each light gets an ON event and an OFF event. An event fires at a
              clock time, or at sunrise or sunset with an offset — the sun times
              arrive in the same OpenWeatherMap response the Home tab already
              fetches, so they cost no extra call and track the seasons on their
              own.

              Like the temperature rules these are EDGE triggered: a light is
              switched as the moment passes, never held there, so a manual tap
              afterwards stands until the next event. Schedules and temperature
              rules can both drive the same light; whichever fires last wins,
              which is the same way a manual tap behaves.
===========================================================================
*/
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>
#include "RelayControl.h"

enum SchedTrigger {
    SCHED_NONE = 0,     // this event is disabled
    SCHED_TIME,         // at hour:minute
    SCHED_SUNRISE,      // at sunrise, plus or minus the offset
    SCHED_SUNSET,       // at sunset, plus or minus the offset
    SCHED_TRIGGER_COUNT
};

struct SchedEvent {
    uint8_t trigger;    // SchedTrigger
    uint8_t hour;       // SCHED_TIME only, 0..23
    uint8_t minute;     // SCHED_TIME only, 0..55 in steps of 5
    int8_t  offsetQ;    // sun triggers only: quarter hours, -8..+8 (= +/- 2h)
};

struct LightSchedule {
    SchedEvent on;
    SchedEvent off;
};

// Largest sun offset, in quarter hours.
static const int8_t SCHED_OFFSET_Q_MAX = 8;

// Load saved schedules. Call once in setup(), after RELAY_init().
void SCHED_begin(void);

// Call every loop(): fires any event whose moment has just passed. Internally
// rate limited, and does nothing until NTP has produced a real clock.
void SCHED_tick(void);

bool SCHED_save(void);
bool SCHED_isDirty(void);

LightSchedule SCHED_get(uint8_t light);

void SCHED_setTrigger(uint8_t light, bool onEvent, uint8_t trigger);
void SCHED_adjustHour(uint8_t light, bool onEvent, int delta);
void SCHED_adjustMinute(uint8_t light, bool onEvent, int delta);   // 5-minute steps
void SCHED_adjustOffset(uint8_t light, bool onEvent, int deltaQ);  // quarter hours

const char* SCHED_triggerName(uint8_t trigger);

// Local minute of day the event resolves to, or -1 if it is disabled or the
// sun times are not known yet.
int SCHED_resolveMinutes(const SchedEvent& e);

// One-line summary for the list page, e.g. "On sunset-15   Off 23:00".
void SCHED_summary(uint8_t light, char* out, size_t outSize);

#endif // SCHEDULE_H
