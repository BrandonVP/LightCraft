/*
===========================================================================
Name        : NetHistory.h
Author      : Brandon Van Pelt
Description : A rolling day of internet reachability, for the connection graph.

              Every minute the panel opens a TCP connection to a public address
              and times how long it takes. Five of those land in each five-minute
              slot, 288 slots covering 24 hours — the same shape as the room
              temperature history.

              A slot keeps the average latency of the probes that succeeded and
              a count of the ones that did not, so a brief blip still shows even
              though the slot as a whole has a latency to plot.

              A failure is classified: WiFi not associated is a different
              problem from WiFi up but nothing reachable beyond it, and the
              graph says which.

              The probe blocks for up to a couple of seconds, so it runs on the
              core-0 network task with the weather fetches, never on the UI loop.
===========================================================================
*/
#ifndef NETHISTORY_H
#define NETHISTORY_H

#include <Arduino.h>

#define NETHIST_SLOTS    288                    // 24 h
#define NETHIST_SLOT_MS  (5UL * 60UL * 1000UL)  // per slot
#define NETHIST_PROBE_MS (60UL * 1000UL)        // one probe a minute

// Latency for a slot in which nothing got through.
static const int16_t NETHIST_NO_DATA = -1;

// Why the last probe failed, for the status line.
enum NetProbeState {
    NET_PROBE_OK = 0,
    NET_PROBE_NO_WIFI,      // not associated
    NET_PROBE_NO_ROUTE      // associated, but nothing answered out there
};

struct NetSlot {
    int16_t latencyMs;      // mean of the probes that succeeded, or NETHIST_NO_DATA
    uint8_t probes;
    uint8_t failures;
};

void NETHIST_begin(void);

// Call from the core-0 network task. Probes when one is due and closes off each
// slot in turn. BLOCKS for up to the probe timeout — never call it from loop().
void NETHIST_taskTick(void);

uint16_t NETHIST_count(void);
uint32_t NETHIST_updateCount(void);   // bumps as each slot closes

// Copy the most recent `n` slots, oldest first, so out[n-1] is the newest.
void NETHIST_copy(NetSlot* out, uint16_t n);

// The most recent probe.
NetProbeState NETHIST_lastState(void);
int16_t       NETHIST_lastLatency(void);   // valid when the last probe was OK

// Over everything held: best/worst/mean latency, and how many slots saw a
// failure. False when nothing has been recorded yet.
bool NETHIST_stats(int16_t& minMs, int16_t& maxMs, int16_t& avgMs, uint16_t& badSlots);

#endif // NETHISTORY_H
