/*
===========================================================================
Name        : WeatherIcons.h
Author      : Brandon Van Pelt
Description : Weather condition icons, drawn from primitives rather than
              stored as bitmaps.

              Flat filled shapes with a contrast outline, in the style of the
              reference sheet (weatherIcons.png). Because they are drawn, one
              icon scales to any size (large on the Home card, small in the
              forecast rows) and the outline follows the active theme, which a
              baked bitmap with black outlines could not do on a dark palette.

              This is the one app-layer module that talks to Arduino_GFX
              directly: EmbeddedGFX's IDisplay exposes only rect/round-rect
              primitives, and these need circles, lines and spans. Call
              wicon_begin() once with the same Arduino_GFX the adapter wraps.
===========================================================================
*/
#ifndef WEATHERICONS_H
#define WEATHERICONS_H

#include <stdint.h>

class Arduino_GFX;

enum WeatherIcon : uint8_t {
    WICON_NONE = 0,     // nothing known yet — draws a placeholder
    WICON_CLEAR_DAY,
    WICON_CLEAR_NIGHT,
    WICON_FEW_DAY,      // sun behind a cloud
    WICON_FEW_NIGHT,    // moon behind a cloud
    WICON_CLOUDS,
    WICON_OVERCAST,
    WICON_SHOWERS,
    WICON_RAIN,
    WICON_STORM,
    WICON_SNOW,
    WICON_MIST
};

// Bind the drawing surface. Call once in setup(), before any wicon_draw().
void wicon_begin(Arduino_GFX* gfx);

// Map an OpenWeatherMap icon code ("01d", "10n", ...) to an icon.
WeatherIcon wicon_fromOwm(const char* iconCode);

// Draw `icon` centred on (cx, cy) inside a square `size` px across. `bg` is the
// colour behind the icon (the card fill): it picks the outline shade and fills
// the box before drawing, so an icon can be redrawn in place.
void wicon_draw(int cx, int cy, int size, WeatherIcon icon, uint16_t bg);

// Blank an icon box back to `bg` — for a slot that has no condition to show.
void wicon_clear(int cx, int cy, int size, uint16_t bg);

#endif // WEATHERICONS_H
