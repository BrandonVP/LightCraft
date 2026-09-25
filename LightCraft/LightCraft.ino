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
                 Settings - theme picker, room-temperature rules

               Behaviour: turning a light on (from the Switches tab) returns to
               the Home tab 30s later. Each light can also carry a temperature
               rule (Settings > Temp Rules) that switches it when the room
               reading from the weather station crosses a setpoint.

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
#include <apps/KeyboardApp.h>   // library on-screen keyboard
#include "ArduinoGFXAdapter.h"
#include "GT911Adapter.h"
#include "RelayControl.h"
#include "HomeApp.h"
#include "SwitchesApp.h"
#include "WeatherTime.h"
#include "WeatherIcons.h"
#include "ForecastApp.h"
#include "TempControl.h"
#include "TempRuleApp.h"
#include "WiFiConfig.h"
#include "WiFiApp.h"

// Give the Arduino loop task extra stack headroom (draw call chains + WiFi).
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// --- Display (Arduino_GFX ST7701 RGB panel, from the seller example) --------
#define GFX_BL 38

// Pixel clock for the RGB panel (Arduino_ESP32RGBPanel::begin() feeds this
// straight into esp_lcd's pclk_hz).
//
// The framebuffer lives in PSRAM and the LCD peripheral streams it out
// continuously with no bounce buffer, so at 16 MHz the panel alone is pulling
// ~32 MB/s through the MSPI bus it SHARES with flash. Any stall on that bus —
// a flash cache miss on a cold code path (WiFi callbacks, HTTP, JSON, an NVS
// write), or PSRAM traffic from the other core — starves the line FIFO, and the
// whole image steps sideways for a frame or two and snaps back. Lowering the
// clock buys the FIFO slack to ride those stalls out.
//
// 480x480 plus porches = 548 x 518 px per frame:
//   16 MHz ~= 56 Hz   14 MHz ~= 49 Hz   12 MHz ~= 42 Hz
// Step down until the shifting stops; the cost is refresh rate.
#define PANEL_PCLK_HZ 14000000

// Diagnostic: set to 0 to build with WiFi/NTP/weather disabled.
#define WEATHER_ENABLE 1

// ST7701 command lines (3-wire SPI) used only to send the panel init sequence.
Arduino_DataBus *panel_init_bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* SDA/MOSI */, GFX_NOT_DEFINED /* MISO */);

// 16-bit parallel RGB data bus + panel timing (porches from the seller example).
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    0 /* pclk_active_neg — seller's proven default; 1 samples on the wrong clock edge and flickers */,
    GFX_NOT_DEFINED /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    0 /* bounce_buffer_size_px: disabled, its refill ISR is not IRAM-safe and faults under load */);

// Newer GFX_Library_for_Arduino: Arduino_RGB_Display replaces Arduino_ST7701_RGBPanel;
// the ST7701 init runs over panel_init_bus. Derives from Arduino_GFX, so the
// EmbeddedGFX adapter is unchanged.
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    panel_init_bus, GFX_NOT_DEFINED /* RST */,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

// --- Touch (GT911; pins/orientation from GT911Adapter.h) --------------------
TAMC_GT911 ts(GT911_SDA, GT911_SCL, GT911_INT, GT911_RST,
              GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT);

// --- EmbeddedGFX adapters + registry ---------------------------------------
ArduinoGFXAdapter gfxDisplay(*gfx);
GT911Adapter      gfxTouch(ts);
App app;

UserInterfaceClass appButtons[GFX_APP_BUTTON_SIZE];
UserInterfaceClass menuButtons[GFX_MENU_BUTTON_SIZE];

// Post-begin ST7701 fixups the newer GFX_Library_for_Arduino doesn't do for us.
// It dropped Arduino_ST7701_RGBPanel, whose begin() ran two commands after the
// shared st7701_type1_init_operations table that Arduino_RGB_Display omits:
//   1. invertDisplay(false) -> 0x20. The init table ends with 0x21 (inversion
//      ON); without the override every color comes out inverted (dark theme
//      renders as tan/green/purple). This is the dominant fix.
//   2. setRotation(0) with bgr=true -> MADCTL 0x36 = 0x00 (BGR). The table never
//      writes 0x36, so red/blue would otherwise be swapped.
void applyPanelColorFixups()
{
    // 1. Inversion OFF (undo the table's 0x21).
    panel_init_bus->sendCommand(0x20);

    // 2. Color order = BGR (mirrors the seller's setRotation(0) with bgr=true).
    panel_init_bus->beginWrite();
    // Y direction
    panel_init_bus->writeCommand(0xFF);
    panel_init_bus->write(0x77); panel_init_bus->write(0x01);
    panel_init_bus->write(0x00); panel_init_bus->write(0x00); panel_init_bus->write(0x10);
    panel_init_bus->writeCommand(0xC7);
    panel_init_bus->write(0x00);
    // Panel-specific color/data control. The new library's init table leaves
    // 0xCD at 0x08; the seller's table (proven on this exact panel) explicitly
    // sets it to 0x00. 0x08 skews mid-tones while leaving endpoints correct.
    panel_init_bus->writeCommand(0xCD);
    panel_init_bus->write(0x00);
    // X direction + color order (back to user bank, then MADCTL = BGR)
    panel_init_bus->writeCommand(0xFF);
    panel_init_bus->write(0x77); panel_init_bus->write(0x01);
    panel_init_bus->write(0x00); panel_init_bus->write(0x00); panel_init_bus->write(0x00);
    panel_init_bus->writeCommand(0x36);
    panel_init_bus->write(0x00);   // 0x00 = BGR, 0x08 = RGB
    panel_init_bus->endWrite();
}

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
    // Gradient header: deep at the top fading to the theme's menu color.
    GUI_I.fillGradientV(0, 0, GFX_SCREEN_WIDTH, 45, gfxShade(gfxTheme.menuBg, -35), gfxTheme.menuBg);
    // Underline strip below the bar.
    GUI_I.drawSquareBtn(0, 45, GFX_SCREEN_WIDTH, GFX_MENU_BAR_HEIGHT, "", gfxTheme.menuBorder, gfxTheme.menuBorder, gfxTheme.menuBorder, ALIGN_CENTER);

    // Tab rects (used for hit-testing + the active underline). Labels are drawn
    // as plain text straight over the gradient so it shows through — no filled
    // tab boxes (mirrors the SwitchWarden frost look).
    createMenuBtns();

    gfx->setTextSize(2);
    gfx->setTextColor(gfxTheme.menuText);
    for (uint8_t i = 0; i < GFX_MENU_BUTTON_SIZE; i++)
    {
        const char* label = menuButtons[i].getBtnText();
        int16_t bx, by; uint16_t bw, bh;
        gfx->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
        int cx = (menuButtons[i].getXStart() + menuButtons[i].getXStop()) / 2;
        gfx->setCursor(cx - bw / 2, 15);
        gfx->print(label);
    }

    // Underline the active app's tab.
    gfx_menu_id_t activeMenu = app.getActiveMenu();
    if (activeMenu < GFX_MENU_BUTTON_SIZE)
    {
        UserInterfaceClass& mb = menuButtons[activeMenu];
        GUI_I.drawSquareBtn(mb.getXStart(), 45, mb.getXStop(), 50, "", gfxTheme.btnColor, gfxTheme.btnColor, mb.getBorderColor(), ALIGN_CENTER);
    }

    GUI_I.updateScreen();
}

// --- Library-generated pages ------------------------------------------------
// The framework builds the Settings landing list and the Themes grid itself,
// and UserInterfaceClass::setButton() leaves every button at the default text
// size of 11 — which this adapter maps to 1x, a 6x8 px glyph that is unreadable
// on a 480x480 panel. Every hand-built page in this project sets its own size;
// these two are wrapped so they get one too.
static const uint8_t GENERATED_PAGE_TEXT_SIZE = 16;   // -> 2x

static uint8_t scaleGeneratedPage(uint8_t count)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    for (uint8_t i = 0; i < count; i++)
        b[i].setTextSize(GENERATED_PAGE_TEXT_SIZE);
    return count;
}

static uint8_t settingsMenu_createBtns(void) { return scaleGeneratedPage(GFX_createMenu()); }
static uint8_t themes_createBtns(void)       { return scaleGeneratedPage(ThemeApp_createBtns()); }

// --- App registration ------------------------------------------------------
void registerApps()
{
    app.add(MENU_home,     "Home",     APP_HOME,          home_handler,     home_createBtns);
    app.add(MENU_home,     "Forecast", APP_FORECAST,      forecastApp_handler, forecastApp_createBtns);
    app.add(MENU_switches, "Switches", APP_SWITCHES,      switches_handler, switches_createBtns);
    app.add(MENU_settings, "Settings", APP_SETTINGS_MENU, GFX_menuInput,     settingsMenu_createBtns);
    app.add(MENU_settings, "Themes",   APP_THEME,         ThemeApp_handler,  themes_createBtns);
    app.add(MENU_settings, "Temp Rules", APP_TEMP_RULES,  temprule_handler,  temprule_createBtns);
    app.add(MENU_settings, "WiFi",     APP_WIFI,          wifiApp_handler,   wifiApp_createBtns);

    // On MENU_hidden so it never shows up in the Settings list — it is opened
    // by whatever page needs a string, and returns there.
    app.add(MENU_hidden,   "Keyboard", APP_KEYBOARD,      KeyboardApp_handler, KeyboardApp_createBtns);
}

// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    RELAY_init();               // lights off at boot
    TEMPCTL_begin();            // load the saved room-temperature rules (NVS)

    // Touch
    Wire.begin(GT911_SDA, GT911_SCL);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    // Display
    gfx->begin(PANEL_PCLK_HZ);
    applyPanelColorFixups();    // undo the table's inversion + set BGR order
    gfx->fillScreen(0x0000);   // black
    gfx->setTextWrap(false);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    GUI_I.begin(gfxDisplay, gfxTouch, appButtons, menuButtons);
    GUI_I.setApp(&app);

    // Weather icons are drawn with circle/line primitives the IDisplay
    // interface does not carry, so they talk to Arduino_GFX directly.
    wicon_begin(gfx);

    ThemeApp_setMenuRedraw(drawMenuBar);
    ThemeApp_begin();

    home_seedClockFromBuild();   // placeholder clock until NTP syncs
    WIFICFG_begin();             // saved credentials, or the secrets.h fallback
#if WEATHER_ENABLE
    weather_begin();             // start WiFi (non-blocking); NTP + weather follow
#endif

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

    // Redraw the whole menu bar when the active tab changes, so the header
    // gradient, tab labels and active underline are always clean (the per-tap
    // underline update can leave a sliver of the old bar behind).
    static gfx_menu_id_t lastMenu = (gfx_menu_id_t)0xFF;
    gfx_menu_id_t nowMenu = app.getActiveMenu();
    if (nowMenu != lastMenu)
    {
        drawMenuBar();
        lastMenu = nowMenu;
    }

    TEMPCTL_tick();             // room-temperature rules drive the relays
    switches_tick();            // 30s return-to-Home after a light turns on
    home_tick();                // live clock + weather while the Home tab is showing
    forecastApp_tick();         // rebuild the forecast page when new data lands
    temprule_tick();            // live room temp + hold-to-repeat on Temp Rules
    wifiApp_tick();             // scan results + live link status on Settings > WiFi
}
