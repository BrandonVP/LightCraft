/*
===========================================================================
Name        : NetHistory.cpp
Author      : Brandon Van Pelt
Description : Internet reachability history (see NetHistory.h).
===========================================================================
*/
#include "NetHistory.h"

#include <WiFi.h>

// Cloudflare's resolver, by address so the probe does not depend on DNS — a
// name lookup would be a second thing that can fail and would add its own
// latency to every reading. Port 80 rather than 53: outbound 53 is blocked on
// some networks, and a blocked port would read as a permanent outage.
static const IPAddress  PROBE_IP(1, 1, 1, 1);
static const uint16_t   PROBE_PORT = 80;
static const int32_t    PROBE_TIMEOUT_MS = 2000;

static NetSlot  s_slot[NETHIST_SLOTS];
static uint16_t s_head   = 0;
static uint16_t s_filled = 0;
static uint32_t s_updates = 0;

// Accumulator for the slot being collected.
static uint32_t s_sum = 0;
static uint8_t  s_ok = 0;
static uint8_t  s_fail = 0;

static uint32_t s_slotStartMs = 0;
static uint32_t s_lastProbeMs = 0;

static NetProbeState s_lastState = NET_PROBE_NO_WIFI;
static int16_t       s_lastLatency = 0;

void NETHIST_begin(void)
{
    for (uint16_t i = 0; i < NETHIST_SLOTS; i++)
    {
        s_slot[i].latencyMs = NETHIST_NO_DATA;
        s_slot[i].probes = 0;
        s_slot[i].failures = 0;
    }

    s_head = s_filled = 0;
    s_updates = 0;
    s_sum = 0;
    s_ok = s_fail = 0;
    s_slotStartMs = millis();
    // Offset so the first probe happens promptly rather than a minute in.
    s_lastProbeMs = millis() - NETHIST_PROBE_MS;
    s_lastState = NET_PROBE_NO_WIFI;
    s_lastLatency = 0;
}

static void commit(void)
{
    NetSlot& s = s_slot[s_head];
    s.probes   = (uint8_t)(s_ok + s_fail);
    s.failures = s_fail;
    s.latencyMs = s_ok ? (int16_t)(s_sum / s_ok) : NETHIST_NO_DATA;

    s_head = (uint16_t)((s_head + 1) % NETHIST_SLOTS);
    if (s_filled < NETHIST_SLOTS)
        s_filled++;
    s_updates++;

    s_sum = 0;
    s_ok = s_fail = 0;
}

// One reachability test. Blocks until it connects or the timeout expires.
static void probe(void)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        s_lastState = NET_PROBE_NO_WIFI;
        s_fail++;
        return;
    }

    NetworkClient client;
    const uint32_t t0 = millis();

    if (!client.connect(PROBE_IP, PROBE_PORT, PROBE_TIMEOUT_MS))
    {
        s_lastState = NET_PROBE_NO_ROUTE;
        s_fail++;
        return;
    }

    uint32_t dt = millis() - t0;
    client.stop();

    if (dt > 9999) dt = 9999;
    s_lastLatency = (int16_t)dt;
    s_lastState = NET_PROBE_OK;
    s_sum += dt;
    s_ok++;
}

void NETHIST_taskTick(void)
{
    if (millis() - s_lastProbeMs >= NETHIST_PROBE_MS)
    {
        s_lastProbeMs = millis();
        probe();
    }

    if (millis() - s_slotStartMs >= NETHIST_SLOT_MS)
    {
        s_slotStartMs += NETHIST_SLOT_MS;   // keep slots on a fixed cadence
        commit();
    }
}

uint16_t NETHIST_count(void)       { return s_filled; }
uint32_t NETHIST_updateCount(void) { return s_updates; }

NetProbeState NETHIST_lastState(void)   { return s_lastState; }
int16_t       NETHIST_lastLatency(void) { return s_lastLatency; }

void NETHIST_copy(NetSlot* out, uint16_t n)
{
    if (!out || n == 0)
        return;

    for (uint16_t i = 0; i < n; i++)
    {
        out[i].latencyMs = NETHIST_NO_DATA;
        out[i].probes = 0;
        out[i].failures = 0;
    }

    const uint16_t take = (n < s_filled) ? n : s_filled;
    for (uint16_t k = 0; k < take; k++)
    {
        const uint16_t src = (uint16_t)((s_head + NETHIST_SLOTS - 1 - k) % NETHIST_SLOTS);
        out[n - 1 - k] = s_slot[src];
    }
}

bool NETHIST_stats(int16_t& minMs, int16_t& maxMs, int16_t& avgMs, uint16_t& badSlots)
{
    uint32_t sum = 0;
    uint16_t n = 0;
    badSlots = 0;
    bool any = false;

    for (uint16_t i = 0; i < NETHIST_SLOTS; i++)
    {
        const NetSlot& s = s_slot[i];
        if (s.probes == 0)
            continue;                    // slot never collected

        if (s.failures)
            badSlots++;

        if (s.latencyMs == NETHIST_NO_DATA)
            continue;

        if (!any) { minMs = maxMs = s.latencyMs; any = true; }
        else
        {
            if (s.latencyMs < minMs) minMs = s.latencyMs;
            if (s.latencyMs > maxMs) maxMs = s.latencyMs;
        }
        sum += (uint32_t)s.latencyMs;
        n++;
    }

    avgMs = n ? (int16_t)(sum / n) : 0;
    return any || badSlots > 0;
}
