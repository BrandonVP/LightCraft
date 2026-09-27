/*
===========================================================================
Name        : TempHistory.h
Author      : Brandon Van Pelt
Description : A rolling day of room temperature, for the history graph.

              The weather station is polled every 45s and only the latest
              reading was ever kept. This bins those into 5-minute slots —
              288 of them, so a full 24 hours — and averages each bin, which
              matters because the DHT11 quantises to whole degrees C (1.8F)
              and a raw trace looks like a staircase.

              A slot with no readings is stored empty rather than skipped, so
              one slot is always five minutes of wall clock and a period with
              the node offline shows as a gap in the line instead of silently
              compressing the axis.

              Slots hold TENTHS of a degree F. The readings coming in are whole
              degrees, but averaging six or seven of them per slot genuinely
              recovers the fraction — and keeping it is the difference between a
              smooth trace and a staircase, since the sensor's own steps are
              1 C (1.8 F) apart.

              RAM only: 576 bytes, and a reboot starts a fresh day. Persisting
              it would mean an NVS write every five minutes, which is not worth
              the flash wear for a graph.
===========================================================================
*/
#ifndef TEMPHISTORY_H
#define TEMPHISTORY_H

#include <Arduino.h>

#define TEMPHIST_SLOTS   288                    // 24 h
#define TEMPHIST_SLOT_MS (5UL * 60UL * 1000UL)  // per slot

// Value stored for a slot that collected no readings.
static const int16_t TEMPHIST_EMPTY = INT16_MIN;

void TEMPHIST_begin(void);

// Call every loop(): collects readings and closes off each slot in turn.
void TEMPHIST_tick(void);

// Slots filled so far, 0..TEMPHIST_SLOTS (counting empty ones).
uint16_t TEMPHIST_count(void);

// Bumps every time a slot closes, so a page can tell when to redraw.
uint32_t TEMPHIST_updateCount(void);

// Copy the most recent `n` slots, oldest first, so out[n-1] is the newest.
// Padded at the front with TEMPHIST_EMPTY when there is not that much history.
// Values are tenths of a degree F.
void TEMPHIST_copy(int16_t* out, uint16_t n);

// Lowest and highest real readings held, in tenths of a degree F. False when
// there are none.
bool TEMPHIST_range(int16_t& minTenthsF, int16_t& maxTenthsF);

#endif // TEMPHISTORY_H
