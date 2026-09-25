/*
===========================================================================
Name        : WiFiApp.cpp
Author      : Brandon Van Pelt
Description : Settings > WiFi (see WiFiApp.h).
===========================================================================
*/
#include "WiFiApp.h"
#include "WiFiConfig.h"
#include "WeatherTime.h"
#include <apps/KeyboardApp.h>
#include <App.h>
#include <WiFi.h>

// The keyboard is the biggest page in the project; make sure the shared button
// array can actually hold it.
static_assert(GFX_APP_BUTTON_SIZE >= KEYBOARDAPP_BUTTONS,
              "GFX_APP_BUTTON_SIZE is too small for the on-screen keyboard");

// --- Button layout ---------------------------------------------------------
static const uint8_t ROWS_PER_PAGE = 5;

enum {
    IDX_STATUS1 = 0,
    IDX_STATUS2,
    IDX_ROW0,
    IDX_SCAN = IDX_ROW0 + ROWS_PER_PAGE,
    IDX_MORE,
    IDX_FORGET,
    WIFI_BTN_COUNT
};

// --- Click returns ---------------------------------------------------------
static const int CR_ROW_BASE = 1;    // 1..5
static const int CR_SCAN     = 10;
static const int CR_MORE     = 11;
static const int CR_FORGET   = 12;

// --- Geometry --------------------------------------------------------------
static const int ROW_Y0    = 150;
static const int ROW_PITCH = 54;
static const int ROW_H     = 48;

// --- Scan results ----------------------------------------------------------
#define WIFIAPP_MAX_NETS 16

struct NetInfo {
    char   ssid[WIFICFG_SSID_LEN];
    int8_t rssi;
    bool   secured;
};

static NetInfo s_nets[WIFIAPP_MAX_NETS];
static uint8_t s_netCount = 0;
static uint8_t s_page     = 0;
static bool    s_scanning = false;

// SSID awaiting a password from the keyboard.
static char s_pendingSsid[WIFICFG_SSID_LEN];

static uint16_t cardFill(void) { return gfxShade(gfxTheme.background, 15); }

static uint8_t pageCount(void)
{
    if (s_netCount == 0) return 1;
    return (uint8_t)((s_netCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
}

// --- Labels (text only; no drawing) ----------------------------------------
static void setStatusLabels(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    if (WiFi.status() == WL_CONNECTED)
    {
        b[IDX_STATUS1].setTextFormat("Connected: %.24s", WiFi.SSID().c_str());
        b[IDX_STATUS2].setTextFormat("%s   %d dBm",
                                     WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    }
    else if (s_scanning)
    {
        b[IDX_STATUS1].setTextFormat("Scanning...");
        b[IDX_STATUS2].setTextFormat("%.28s", WIFICFG_ssid());
    }
    else
    {
        b[IDX_STATUS1].setTextFormat("Connecting: %.22s", WIFICFG_ssid());
        b[IDX_STATUS2].setText(WIFICFG_isStored() ? "saved network" : "built-in default (secrets.h)");
    }
}

static void setRowLabels(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();

    for (uint8_t r = 0; r < ROWS_PER_PAGE; r++)
    {
        uint8_t n = (uint8_t)(s_page * ROWS_PER_PAGE + r);
        UserInterfaceClass& row = b[IDX_ROW0 + r];

        if (n < s_netCount)
        {
            // '*' marks a secured network. The built-in font is fixed width, so
            // padding lines the signal column up.
            row.setTextFormat("%-20.20s %c %3d", s_nets[n].ssid,
                              s_nets[n].secured ? '*' : ' ', (int)s_nets[n].rssi);
            row.setClickable(true);
            row.setBgColor(gfxTheme.btnColor);
            row.setBorderColor(gfxTheme.btnBorder);
            row.setTextColor(gfxTheme.btnText);
        }
        else
        {
            row.setText(n == 0 && !s_scanning ? "  (tap SCAN to list networks)" : "");
            row.setClickable(false);
            row.setBgColor(gfxTheme.background);
            row.setBorderColor(gfxTheme.background);
            row.setTextColor(gfxShade(gfxTheme.btnTextColor, -35));
        }
    }

    // With one page there is nothing to cycle to, so the button says so rather
    // than looking like it failed to respond.
    const bool multiPage = pageCount() > 1;
    b[IDX_MORE].setTextFormat("PAGE %u/%u", (unsigned)(s_page + 1), (unsigned)pageCount());
    b[IDX_MORE].setClickable(multiPage);
    b[IDX_MORE].setBgColor(multiPage ? gfxTheme.btnColor : gfxTheme.background);
    b[IDX_MORE].setBorderColor(multiPage ? gfxTheme.btnBorder : gfxShade(gfxTheme.background, 20));
    b[IDX_MORE].setTextColor(multiPage ? gfxTheme.btnText : gfxShade(gfxTheme.btnTextColor, -45));
}

// --- Scanning --------------------------------------------------------------
static void startScan(void)
{
    if (s_scanning)
        return;

    WiFi.scanDelete();
    // Async: the UI loop must not block for the seconds a scan takes.
    WiFi.scanNetworks(true /* async */, false /* show hidden */);
    s_scanning = true;
    s_page = 0;
}

static void collectScan(void)
{
    int found = WiFi.scanComplete();
    if (found < 0)
        return;                     // -1 running, -2 not started

    s_netCount = 0;
    for (int i = 0; i < found && s_netCount < WIFIAPP_MAX_NETS; i++)
    {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0)
            continue;               // hidden / unusable

        // Skip duplicates: mesh networks advertise the same SSID per band.
        bool seen = false;
        for (uint8_t k = 0; k < s_netCount && !seen; k++)
            seen = (strcmp(s_nets[k].ssid, ssid.c_str()) == 0);
        if (seen)
            continue;

        strncpy(s_nets[s_netCount].ssid, ssid.c_str(), WIFICFG_SSID_LEN - 1);
        s_nets[s_netCount].ssid[WIFICFG_SSID_LEN - 1] = '\0';
        s_nets[s_netCount].rssi    = (int8_t)WiFi.RSSI(i);
        s_nets[s_netCount].secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        s_netCount++;
    }

    WiFi.scanDelete();
    s_scanning = false;
    Serial.printf("[WiFiApp] scan found %u usable networks\n", (unsigned)s_netCount);
}

// --- Keyboard hand-off -----------------------------------------------------
static void onPasswordEntered(const char* text, bool accepted)
{
    if (!accepted)
        return;

    if (WIFICFG_save(s_pendingSsid, text))
        weather_reconnect();
}

// --- Page ------------------------------------------------------------------
uint8_t wifiApp_createBtns(void)
{
    UserInterfaceClass* b = GUI_I.appButtons();
    const uint16_t fill   = cardFill();
    const uint16_t shadow = 0x0000;
    const uint16_t dim    = gfxShade(gfxTheme.btnTextColor, -30);

    GUI_I.drawCard(24, 56, 432, 84, 16, fill, shadow, 5);

    b[IDX_STATUS1].setButton(44, 64, 436, 96, 0, true, 10, "", ALIGN_LEFT, fill, fill, gfxTheme.btnTextColor);
    b[IDX_STATUS1].setTextSize(16); b[IDX_STATUS1].setClickable(false);

    b[IDX_STATUS2].setButton(44, 100, 436, 132, 0, true, 10, "", ALIGN_LEFT, fill, fill, dim);
    b[IDX_STATUS2].setTextSize(16); b[IDX_STATUS2].setClickable(false);

    for (uint8_t r = 0; r < ROWS_PER_PAGE; r++)
    {
        int y1 = ROW_Y0 + r * ROW_PITCH;
        b[IDX_ROW0 + r].setButton(24, y1, 456, y1 + ROW_H, (uint16_t)(CR_ROW_BASE + r), true, 12,
                                  "", ALIGN_LEFT, gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
        b[IDX_ROW0 + r].setTextSize(16);
    }

    b[IDX_SCAN].setButton(24, 424, 170, 470, CR_SCAN, true, 14, "SCAN", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_SCAN].setTextSize(16);

    b[IDX_MORE].setButton(180, 424, 300, 470, CR_MORE, true, 14, "PAGE 1/1", ALIGN_CENTER,
                          gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_MORE].setTextSize(16);

    b[IDX_FORGET].setButton(310, 424, 456, 470, CR_FORGET, true, 14, "FORGET", ALIGN_CENTER,
                            gfxTheme.btnColor, gfxTheme.btnBorder, gfxTheme.btnText);
    b[IDX_FORGET].setTextSize(16);

    // Opening the page with nothing to show is a dead end — scan straight away.
    if (s_netCount == 0 && !s_scanning)
        startScan();

    setStatusLabels();
    setRowLabels();

    return WIFI_BTN_COUNT;
}

static void refreshList(void)
{
    setRowLabels();
    for (uint8_t r = 0; r < ROWS_PER_PAGE; r++)
        GUI_I.updateButton(IDX_ROW0 + r);
    GUI_I.updateButton(IDX_MORE);
    GUI_I.updateScreen();
}

void wifiApp_handler(int userInput)
{
    if (userInput < 0)
        return;

    if (userInput == CR_SCAN)
    {
        startScan();
        setStatusLabels();
        GUI_I.updateButton(IDX_STATUS1);
        GUI_I.updateScreen();
        return;
    }

    if (userInput == CR_MORE)
    {
        s_page = (uint8_t)((s_page + 1) % pageCount());
        refreshList();
        return;
    }

    if (userInput == CR_FORGET)
    {
        WIFICFG_clear();
        weather_reconnect();
        setStatusLabels();
        GUI_I.updateButton(IDX_STATUS1);
        GUI_I.updateButton(IDX_STATUS2);
        GUI_I.updateScreen();
        return;
    }

    if (userInput < CR_ROW_BASE || userInput >= CR_ROW_BASE + ROWS_PER_PAGE)
        return;

    uint8_t n = (uint8_t)(s_page * ROWS_PER_PAGE + (userInput - CR_ROW_BASE));
    if (n >= s_netCount)
        return;

    strncpy(s_pendingSsid, s_nets[n].ssid, sizeof(s_pendingSsid) - 1);
    s_pendingSsid[sizeof(s_pendingSsid) - 1] = '\0';

    // An open network needs no password — save and reconnect straight away.
    if (!s_nets[n].secured)
    {
        if (WIFICFG_save(s_pendingSsid, ""))
            weather_reconnect();
        setStatusLabels();
        GUI_I.updateButton(IDX_STATUS1);
        GUI_I.updateButton(IDX_STATUS2);
        GUI_I.updateScreen();
        return;
    }

    char prompt[32];
    snprintf(prompt, sizeof(prompt), "Password: %.20s", s_pendingSsid);
    KeyboardApp_open(prompt, "", WIFICFG_PASS_LEN - 1, true /* mask */, APP_WIFI, onPasswordEntered);

    App* app = GUI_I.getApp();
    if (app) app->newApp(APP_KEYBOARD);
}

void wifiApp_tick(void)
{
    App* app = GUI_I.getApp();

    if (!app || app->getActiveApp() != APP_WIFI || app->renderState != App::APP_STATE_DONE)
        return;

    if (s_scanning)
    {
        uint8_t before = s_netCount;
        collectScan();
        if (!s_scanning)
        {
            (void)before;
            refreshList();
            setStatusLabels();
            GUI_I.updateButton(IDX_STATUS1);
            GUI_I.updateButton(IDX_STATUS2);
            GUI_I.updateScreen();
        }
    }

    // Link state moves on its own (association, DHCP, signal), so keep the
    // status card current while the page is open.
    static uint32_t lastMs = 0;
    if (millis() - lastMs < 500)
        return;
    lastMs = millis();

    const char* before = GUI_I.appButtons()[IDX_STATUS1].getBtnText();
    char previous[32];
    strncpy(previous, before ? before : "", sizeof(previous) - 1);
    previous[sizeof(previous) - 1] = '\0';

    setStatusLabels();
    if (strcmp(previous, GUI_I.appButtons()[IDX_STATUS1].getBtnText()) != 0)
    {
        GUI_I.updateButton(IDX_STATUS1);
        GUI_I.updateButton(IDX_STATUS2);
        GUI_I.updateScreen();
    }
}
