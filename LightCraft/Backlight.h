/*
===========================================================================
Name        : Backlight.h
Author      : Brandon Van Pelt
Description : Backlight brightness, idle dimming and touch wake.

              The panel is mains powered and never sleeps, so left alone it
              glows at full brightness all night. This drives GPIO 38 with LEDC
              PWM instead of a plain HIGH: full while someone is using it, down
              to the idle level after a period with no touch, and back up the
              moment the panel is touched.

              A short idle timeout does most of the work a night schedule would:
              the screen is only bright when somebody is standing at it.

              Costs no radio and no flash traffic, so unlike most additions it
              cannot make the RGB panel's scanout glitching any worse.
===========================================================================
*/
#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <Arduino.h>

// Limits used by the settings page.
static const uint8_t BL_ACTIVE_MIN = 10;   // never let the panel go dark while in use
static const uint8_t BL_ACTIVE_MAX = 100;
static const uint8_t BL_IDLE_MAX   = 80;   // idle must stay at or below active
static const uint8_t BL_STEP_PCT   = 5;

// Take over the backlight pin and load saved settings. Call once in setup(),
// in place of the pinMode/digitalWrite that used to drive it.
void BACKLIGHT_begin(uint8_t pin);

// Call every loop, passing whether the panel is being touched right now
// (GUI_I.isTouched()). Runs the idle timer and the fade.
void BACKLIGHT_tick(bool touched);

// True while the touch that woke a dark screen should be ignored. A tap on a
// blacked-out panel would otherwise land on whatever button was underneath.
bool BACKLIGHT_swallowTouch(void);

uint8_t  BACKLIGHT_activePct(void);
uint8_t  BACKLIGHT_idlePct(void);
uint16_t BACKLIGHT_idleSeconds(void);        // 0 = never dim

void BACKLIGHT_setActivePct(uint8_t pct);
void BACKLIGHT_setIdlePct(uint8_t pct);
void BACKLIGHT_setIdleSeconds(uint16_t seconds);

// Selectable idle timeouts, for the settings page.
uint8_t     BACKLIGHT_timeoutCount(void);
uint8_t     BACKLIGHT_timeoutIndex(void);    // index of the current setting
void        BACKLIGHT_setTimeoutIndex(uint8_t index);
const char* BACKLIGHT_timeoutName(uint8_t index);

#endif // BACKLIGHT_H
