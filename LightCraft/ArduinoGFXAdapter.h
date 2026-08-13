/*
===========================================================================
Name        : ArduinoGFXAdapter.h
Author      : Brandon Van Pelt
Description : EmbeddedGFX IDisplay adapter for the Arduino_GFX library
              (ESP32-4848S040: ST7701 480x480 RGB panel).

              Arduino_GFX is Adafruit-GFX-style, so most calls forward directly.
              Text uses the built-in font scaled by setTextSize(); drawn with a
              transparent background so it sits cleanly over an already-filled
              button. The RGB panel draws straight to its PSRAM framebuffer, so
              there is no off-screen buffer to flush.
===========================================================================
*/
#ifndef ARDUINO_GFX_ADAPTER_H
#define ARDUINO_GFX_ADAPTER_H

#include "appConfig.h"      // pulls in <EmbeddedGFX.h> with GFX_* overrides
#include <Arduino_GFX_Library.h>

class ArduinoGFXAdapter : public IDisplay
{
public:
    explicit ArduinoGFXAdapter(Arduino_GFX& gfx) : m_gfx(gfx) {}

    void fillRect(int x, int y, int w, int h, uint16_t color) override            { m_gfx.fillRect(x, y, w, h, color); }
    void drawRect(int x, int y, int w, int h, uint16_t color) override            { m_gfx.drawRect(x, y, w, h, color); }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) override { m_gfx.fillRoundRect(x, y, w, h, r, color); }
    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) override { m_gfx.drawRoundRect(x, y, w, h, r, color); }

    void setTextColor(uint16_t color) override { m_textColor = color; }

    void drawString(const char* str, int len, int x, int y) override
    {
        char buf[64];
        if (len < 0) len = 0;
        if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
        memcpy(buf, str, (size_t)len);
        buf[len] = '\0';

        m_gfx.setTextColor(m_textColor);   // transparent bg over the button fill
        m_gfx.setCursor(x, y);
        m_gfx.print(buf);
    }

    // Built-in font: 6 px advance per char, scaled by the current text size.
    int strPixelLen(const char* str) override { return (int)strlen(str) * 6 * m_textSize; }

    // RGB panel draws directly to its framebuffer — nothing to buffer/flush.
    void useFrameBuffer(bool) override {}
    void updateScreen() override {}

    // Map the framework's logical size onto Adafruit-GFX integer text sizes.
    void setTextSize(uint8_t s) override
    {
        m_textSize = (uint8_t)(s / 8);
        if (m_textSize < 1) m_textSize = 1;
        m_gfx.setTextSize(m_textSize);
    }

    int width() override  { return m_gfx.width(); }
    int height() override { return m_gfx.height(); }

private:
    Arduino_GFX& m_gfx;
    uint16_t m_textColor = 0xFFFF;
    uint8_t  m_textSize = 1;
};

#endif // ARDUINO_GFX_ADAPTER_H
