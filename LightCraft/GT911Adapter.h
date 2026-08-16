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

// The GT911 only samples every ~10-16ms. Two consequences shape this adapter:
//
//  1. Reading it every loop (sub-ms) hammers the ESP32-S3 I2C master driver,
//     which corrupts and crashes under the RGB panel's continuous DMA load
//     (Guru Meditation inside Wire.requestFrom -> i2c_master_receive). Throttle
//     the actual I2C read to ~the report rate; polling faster gains nothing.
//  2. Between fresh samples a raw read returns touches=0, so without smoothing
//     a held finger reads as rapid tap/release (buzzing relays). Keep reporting
//     "held" at the last coordinate until no touch for RELEASE_DEBOUNCE_MS.
#define GT911_READ_INTERVAL_MS    15
#define GT911_RELEASE_DEBOUNCE_MS 40

class GT911Adapter : public ITouch
{
public:
    explicit GT911Adapter(TAMC_GT911& ts) : m_ts(ts) {}

    bool touched() override
    {
        uint32_t now = millis();
        if ((uint32_t)(now - m_lastReadMs) >= GT911_READ_INTERVAL_MS)
        {
            m_lastReadMs = now;
            m_ts.read();

            if (m_ts.isTouched)
            {
                m_lastTouchMs = now;
                m_rawX = m_ts.points[0].x;   // cache the last valid raw point
                m_rawY = m_ts.points[0].y;
                m_held = true;
            }
            else if (m_held && (uint32_t)(now - m_lastTouchMs) >= GT911_RELEASE_DEBOUNCE_MS)
            {
                m_held = false;   // sustained no-touch = real release
            }
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
    uint32_t    m_lastReadMs = 0;
    uint32_t    m_lastTouchMs = 0;
    uint16_t    m_rawX = 0;
    uint16_t    m_rawY = 0;
    bool        m_held = false;
};

#endif // GT911_ADAPTER_H
