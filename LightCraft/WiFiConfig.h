/*
===========================================================================
Name        : WiFiConfig.h
Author      : Brandon Van Pelt
Description : WiFi credentials, stored in NVS instead of compiled in.

              Credentials picked in Settings > WiFi are saved to flash and used
              from then on. If nothing has been saved yet, the WIFI_SSID /
              WIFI_PASSWORD in secrets.h are used as a fallback, so a freshly
              flashed board still comes up on the network exactly as before.

              secrets.h is therefore now a seed, not the source of truth: once
              a network is chosen on the panel, the stored one wins.
===========================================================================
*/
#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include <Arduino.h>

#define WIFICFG_SSID_LEN 33   // 32 + terminator
#define WIFICFG_PASS_LEN 64   // 63 + terminator

// Load saved credentials. Call once in setup(), before weather_begin().
void WIFICFG_begin(void);

const char* WIFICFG_ssid(void);
const char* WIFICFG_password(void);

// True when the credentials came from flash rather than the secrets.h fallback.
bool WIFICFG_isStored(void);

// Save and use a new network. Returns false if the write failed.
bool WIFICFG_save(const char* ssid, const char* password);

// Drop the saved network and fall back to secrets.h.
void WIFICFG_clear(void);

#endif // WIFICONFIG_H
