/*
===========================================================================
Name        : GT911Adapter.h
Author      : Brandon Van Pelt
Description : EmbeddedGFX ITouch adapter for the GT911 capacitive controller
              via the TAMC_GT911 library (ESP32-4848S040).

              Wiring + coordinate mapping taken from the seller's touch.h: I2C
              SDA 19 / SCL 45, both axes inverted (map raw 480..0 -> 0..479).
===========================================================================
*/
#ifndef GT911_ADAPTER_H
#define GT911_ADAPTER_H

#include "appConfig.h"
#include <Wire.h>
#include <TAMC_GT911.h>

// GT911 wiring for the ESP32-4848S040.
#define GT911_SDA 19
#define GT911_SCL 45
#define GT911_INT -1
#define GT911_RST -1

// Panel touch orientation: raw range 480..0 maps to screen 0..(size-1) on both
// axes (from the seller touch.h TOUCH_MAP_* values).
#define GT911_MAP_X1 480
#define GT911_MAP_X2 0
#define GT911_MAP_Y1 480
#define GT911_MAP_Y2 0

// The GT911 only reports a fresh sample every ~10-16ms and returns touches=0
// (and clears its buffer) on any read in between. The main loop polls much
// faster than that, so a raw read oscillates touched/untouched while a finger
// is held down — which the GUI state machine reads as repeated tap/release,
// making a toggle button buzz its relay on and off. Bridge those inter-report
// gaps: keep reporting "held" (at the last valid coordinate) until we've seen
// no touch for RELEASE_DEBOUNCE_MS, comfortably longer than one report period.
#define GT911_RELEASE_DEBOUNCE_MS 40

class GT911Adapter : public ITouch
{
public:
    explicit GT911Adapter(TAMC_GT911& ts) : m_ts(ts) {}

    bool touched() override
    {
        m_ts.read();
        uint32_t now = millis();

        if (m_ts.isTouched)
        {
            m_lastTouchMs = now;
            m_rawX = m_ts.points[0].x;   // cache the last valid raw point
            m_rawY = m_ts.points[0].y;
            m_held = true;
        }
        else if (m_held && (now - m_lastTouchMs) < GT911_RELEASE_DEBOUNCE_MS)
        {
            // Brief gap between GT911 reports — treat as still held.
            return true;
        }
        else
        {
            m_held = false;
        }
        return m_held;
    }

    void getPoint(int& x, int& y) override
    {
        // Map the last valid raw point (cached in touched()), so the coordinate
        // stays put across inter-report gaps.
        x = map(m_rawX, GT911_MAP_X1, GT911_MAP_X2, 0, GFX_SCREEN_WIDTH - 1);
        y = map(m_rawY, GT911_MAP_Y1, GT911_MAP_Y2, 0, GFX_SCREEN_HEIGHT - 1);
    }

private:
    TAMC_GT911& m_ts;
    uint32_t    m_lastTouchMs = 0;
    uint16_t    m_rawX = 0;
    uint16_t    m_rawY = 0;
    bool        m_held = false;
};

#endif // GT911_ADAPTER_H
