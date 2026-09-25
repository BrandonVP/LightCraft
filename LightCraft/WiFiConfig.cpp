/*
===========================================================================
Name        : WiFiConfig.cpp
Author      : Brandon Van Pelt
Description : WiFi credential store (see WiFiConfig.h).
===========================================================================
*/
#include "WiFiConfig.h"
#include "secrets.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "lightcraft";
static const char* NVS_KEY_SSID  = "wifi_ssid";
static const char* NVS_KEY_PASS  = "wifi_pass";

static char s_ssid[WIFICFG_SSID_LEN];
static char s_pass[WIFICFG_PASS_LEN];
static bool s_stored = false;

static void useFallback(void)
{
    strncpy(s_ssid, WIFI_SSID, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, WIFI_PASSWORD, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    s_stored = false;
}

void WIFICFG_begin(void)
{
    useFallback();

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true /* read-only */))
    {
        Serial.println("[WiFiCfg] no saved network — using secrets.h");
        return;
    }

    char ssid[WIFICFG_SSID_LEN] = {};
    char pass[WIFICFG_PASS_LEN] = {};
    size_t gotSsid = prefs.getString(NVS_KEY_SSID, ssid, sizeof(ssid));
    prefs.getString(NVS_KEY_PASS, pass, sizeof(pass));
    prefs.end();

    // An empty SSID is not a usable network, saved or not.
    if (gotSsid == 0 || ssid[0] == '\0')
    {
        Serial.println("[WiFiCfg] no saved network — using secrets.h");
        return;
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    s_stored = true;

    Serial.printf("[WiFiCfg] using saved network '%s'\n", s_ssid);
}

const char* WIFICFG_ssid(void)     { return s_ssid; }
const char* WIFICFG_password(void) { return s_pass; }
bool        WIFICFG_isStored(void) { return s_stored; }

bool WIFICFG_save(const char* ssid, const char* password)
{
    if (!ssid || ssid[0] == '\0')
        return false;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false /* read-write */))
    {
        Serial.println("[WiFiCfg] save failed: cannot open NVS");
        return false;
    }

    bool ok = prefs.putString(NVS_KEY_SSID, ssid) > 0;
    prefs.putString(NVS_KEY_PASS, password ? password : "");
    prefs.end();

    if (!ok)
    {
        Serial.println("[WiFiCfg] save failed: write error");
        return false;
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, password ? password : "", sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    s_stored = true;

    Serial.printf("[WiFiCfg] saved network '%s'\n", s_ssid);
    return true;
}

void WIFICFG_clear(void)
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false))
    {
        prefs.remove(NVS_KEY_SSID);
        prefs.remove(NVS_KEY_PASS);
        prefs.end();
    }

    useFallback();
    Serial.println("[WiFiCfg] cleared — back to secrets.h");
}
