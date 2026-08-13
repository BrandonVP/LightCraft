/*
 ===========================================================================
 Name        : LightCraft.ino
 Author      : Brandon Van Pelt
 Description : 3-gang smart light switch on the Guition ESP32-4848S040
               (ESP32-S3, 480x480 IPS, ST7701 RGB panel via Arduino_GFX, GT911
               capacitive touch), built on the EmbeddedGFX library.

               Tabs:
                 Home     - date / time / weather (placeholder; NTP+API later)
                 Switches - three ON/OFF light toggles in a row
                 Settings - theme picker

               Behaviour: turning a light on (from the Switches tab) returns to
               the Home tab 30s later.

               Display/touch/relay config comes from the seller demos:
                 RGB panel + backlight GPIO 38, GT911 on I2C SDA 19 / SCL 45,
                 relays on GPIO 40 / 2 / 1 (active-high).

               NOTE: build for the ESP32-S3 with OPI PSRAM enabled — the RGB
               panel framebuffer lives in PSRAM.
 ===========================================================================
 */

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TAMC_GT911.h>

#include "appConfig.h"          // GFX overrides + <EmbeddedGFX.h> + project enums
#include <apps/ThemeApp.h>      // library theme picker
#include "ArduinoGFXAdapter.h"
#include "GT911Adapter.h"
#include "RelayControl.h"
#include "HomeApp.h"
#include "SwitchesApp.h"

// --- Display (Arduino_GFX ST7701 RGB panel, from the seller example) --------
#define GFX_BL 38

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    39 /* CS */, 48 /* SCK */, 47 /* SDA */,
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
    rgbpanel, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
    true /* IPS */, 480 /* width */, 480 /* height */,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations), true /* BGR */,
    10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

// --- Touch (GT911; pins/orientation from GT911Adapter.h) --------------------
TAMC_GT911 ts(GT911_SDA, GT911_SCL, GT911_INT, GT911_RST,
              GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);

// --- EmbeddedGFX adapters + registry ---------------------------------------
ArduinoGFXAdapter gfxDisplay(*gfx);
GT911Adapter      gfxTouch(ts);
App app;

UserInterfaceClass appButtons[GFX_APP_BUTTON_SIZE];
UserInterfaceClass menuButtons[GFX_MENU_BUTTON_SIZE];

// --- Menu bar (3 tabs) ------------------------------------------------------
void createMenuBtns()
{
    menuButtons[0].setButton(  5, 0, 158, 45, APP_HOME,          true, 0, "Home",     ALIGN_CENTER, gfxTheme.menuBg, gfxTheme.menuBg, gfxTheme.btnTextColor);
    menuButtons[1].setButton(163, 0, 316, 45, APP_SWITCHES,      true, 0, "Switches", ALIGN_CENTER, gfxTheme.menuBg, gfxTheme.menuBg, gfxTheme.btnTextColor);
    menuButtons[2].setButton(321, 0, 475, 45, APP_SETTINGS_MENU, true, 0, "Settings", ALIGN_CENTER, gfxTheme.menuBg, gfxTheme.menuBg, gfxTheme.btnTextColor);
    for (uint8_t i = 0; i < GFX_MENU_BUTTON_SIZE; i++) menuButtons[i].setTextSize(16);
}

// Draw the top menu bar. Also used as ThemeApp's menu-redraw hook.
void drawMenuBar()
{
    GUI_I.drawSquareBtn(0,  0, GFX_SCREEN_WIDTH, 45, "", gfxTheme.menuBg, gfxTheme.menuBg, gfxTheme.menuBg, ALIGN_CENTER);
    GUI_I.drawSquareBtn(0, 45, GFX_SCREEN_WIDTH, GFX_MENU_BAR_HEIGHT, "", gfxTheme.menuBorder, gfxTheme.menuBorder, gfxTheme.menuBorder, ALIGN_CENTER);

    createMenuBtns();
    uint8_t state = 0;
    while (GUI_I.drawPage(menuButtons, state, GFX_MENU_BUTTON_SIZE));
    GUI_I.setGraphicLoaderState(0);

    gfx_menu_id_t activeMenu = app.getActiveMenu();
    if (activeMenu < GFX_MENU_BUTTON_SIZE)
    {
        UserInterfaceClass& mb = menuButtons[activeMenu];
        GUI_I.drawSquareBtn(mb.getXStart(), 45, mb.getXStop(), 50, "", gfxTheme.btnColor, gfxTheme.btnColor, mb.getBorderColor(), ALIGN_CENTER);
    }

    GUI_I.updateScreen();
}

// --- App registration ------------------------------------------------------
void registerApps()
{
    app.add(MENU_home,     "Home",     APP_HOME,          home_handler,     home_createBtns);
    app.add(MENU_switches, "Switches", APP_SWITCHES,      switches_handler, switches_createBtns);
    app.add(MENU_settings, "Settings", APP_SETTINGS_MENU, GFX_menuInput,    GFX_createMenu);
    app.add(MENU_settings, "Themes",   APP_THEME,         ThemeApp_handler, ThemeApp_createBtns);
}

// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    RELAY_init();               // lights off at boot

    // Touch
    Wire.begin(GT911_SDA, GT911_SCL);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    // Display
    gfx->begin(16000000);
    gfx->fillScreen(BLACK);
    gfx->setTextWrap(false);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    GUI_I.begin(gfxDisplay, gfxTouch, appButtons, menuButtons);
    GUI_I.setApp(&app);

    ThemeApp_setMenuRedraw(drawMenuBar);
    ThemeApp_begin();

    registerApps();
    app.init();                 // first registered app (Home) shows on load

    drawMenuBar();
}

// ---------------------------------------------------------------------------
void loop()
{
    GUI_I.buttonMonitor(menuButtons, GFX_MENU_BUTTON_SIZE);
    GUI_I.updateTouch();
    app.run();
    switches_tick();            // 30s return-to-Home after a light turns on
}
