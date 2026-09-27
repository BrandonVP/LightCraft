/*
===========================================================================
Name        : TempHistory.cpp
Author      : Brandon Van Pelt
Description : Rolling room-temperature history (see TempHistory.h).
===========================================================================
*/
#include "TempHistory.h"
#include "WeatherTime.h"

static int16_t  s_slot[TEMPHIST_SLOTS];
static uint16_t s_head   = 0;      // next slot to write
static uint16_t s_filled = 0;
static uint32_t s_updates = 0;

// Accumulator for the slot currently being collected.
static int32_t  s_sum = 0;
static uint16_t s_samples = 0;
static uint32_t s_slotStartMs = 0;

// Timestamp of the last reading folded in, so the same poll is not counted
// repeatedly — this tick runs thousands of times between fetches.
static uint32_t s_lastStamp = 0;

void TEMPHIST_begin(void)
{
    for (uint16_t i = 0; i < TEMPHIST_SLOTS; i++)
        s_slot[i] = TEMPHIST_EMPTY;

    s_head = s_filled = 0;
    s_sum = 0;
    s_samples = 0;
    s_slotStartMs = millis();
    s_lastStamp = 0;
    s_updates = 0;
}

static void commit(int16_t value)
{
    s_slot[s_head] = value;
    s_head = (uint16_t)((s_head + 1) % TEMPHIST_SLOTS);
    if (s_filled < TEMPHIST_SLOTS)
        s_filled++;
    s_updates++;
}

void TEMPHIST_tick(void)
{
    WeatherData w = weather_get();

    if (w.roomValid && w.roomStampMs != s_lastStamp)
    {
        s_lastStamp = w.roomStampMs;
        s_sum += w.roomTempF;
        s_samples++;
    }

    if (millis() - s_slotStartMs < TEMPHIST_SLOT_MS)
        return;

    s_slotStartMs += TEMPHIST_SLOT_MS;          // keep slots on a fixed cadence

    // Averaged in tenths: the inputs are whole degrees, but the mean of six or
    // seven of them carries real sub-degree information worth keeping.
    // An empty slot is still recorded, so the x axis stays honest about time.
    commit(s_samples ? (int16_t)((s_sum * 10) / s_samples) : TEMPHIST_EMPTY);
    s_sum = 0;
    s_samples = 0;
}

uint16_t TEMPHIST_count(void)       { return s_filled; }
uint32_t TEMPHIST_updateCount(void) { return s_updates; }

void TEMPHIST_copy(int16_t* out, uint16_t n)
{
    if (!out || n == 0)
        return;

    for (uint16_t i = 0; i < n; i++)
        out[i] = TEMPHIST_EMPTY;

    const uint16_t take = (n < s_filled) ? n : s_filled;
    for (uint16_t k = 0; k < take; k++)
    {
        const uint16_t src = (uint16_t)((s_head + TEMPHIST_SLOTS - 1 - k) % TEMPHIST_SLOTS);
        out[n - 1 - k] = s_slot[src];
    }
}

bool TEMPHIST_range(int16_t& minF, int16_t& maxF)
{
    bool any = false;
    for (uint16_t i = 0; i < TEMPHIST_SLOTS; i++)
    {
        const int16_t v = s_slot[i];
        if (v == TEMPHIST_EMPTY)
            continue;

        if (!any) { minF = maxF = v; any = true; }
        else
        {
            if (v < minF) minF = v;
            if (v > maxF) maxF = v;
        }
    }
    return any;
}
