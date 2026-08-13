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

class GT911Adapter : public ITouch
{
public:
    explicit GT911Adapter(TAMC_GT911& ts) : m_ts(ts) {}

    bool touched() override
    {
        m_ts.read();
        return m_ts.isTouched;
    }

    void getPoint(int& x, int& y) override
    {
        // points[0] was populated by the read() in touched().
        x = map(m_ts.points[0].x, GT911_MAP_X1, GT911_MAP_X2, 0, GFX_SCREEN_WIDTH - 1);
        y = map(m_ts.points[0].y, GT911_MAP_Y1, GT911_MAP_Y2, 0, GFX_SCREEN_HEIGHT - 1);
    }

private:
    TAMC_GT911& m_ts;
};

#endif // GT911_ADAPTER_H
