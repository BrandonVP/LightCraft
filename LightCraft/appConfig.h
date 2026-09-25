/*
===========================================================================
Name        : appConfig.h
Author      : Brandon Van Pelt
Description : Project configuration for LightCraft (ESP32-4848S040, 480x480).
              Screen overrides + the project's menu / app id enums. Include this
              instead of <EmbeddedGFX.h> directly so the GFX_* overrides are
              seen first.
===========================================================================
*/
#ifndef APPCONFIG_H
#define APPCONFIG_H

// --- Library configuration overrides (must precede <EmbeddedGFX.h>) --------
#define GFX_SCREEN_WIDTH     480
#define GFX_SCREEN_HEIGHT    480
#define GFX_APP_BUTTON_SIZE  56   // busiest page: the keyboard (48); Control is 23
#define GFX_MENU_BUTTON_SIZE 3

#include <EmbeddedGFX.h>

// --- Top menu tabs ---------------------------------------------------------
enum Menus {
    MENU_home = 0,      // date / time / weather (+ the forecast sub-page)
    MENU_control,       // mini-split + the 3 light toggles
    MENU_settings,      // theme picker, temperature rules, WiFi (+ room for more)
    MENU_hidden         // no tab: pages reached only from another page, so they
                        // never appear in a generated menu list (the keyboard)
};

// --- Apps ------------------------------------------------------------------
enum AppLabels {
    APP_HOME = 0,        // Home tab (opens directly)
    APP_FORECAST,        // Home > 5-day forecast (tap the weather card)
    APP_CONTROL,         // Control tab (opens directly): mini-split + lights
    APP_SETTINGS_MENU,   // Settings tab landing (lists settings apps)
    APP_THEME,           // Settings > Themes
    APP_TEMP_RULES,      // Settings > Temp Rules
    APP_WIFI,            // Settings > WiFi
    APP_KEYBOARD,        // text entry, opened by whatever needs a string
    APP_COUNT
};

#endif // APPCONFIG_H
