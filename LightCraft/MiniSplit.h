/*
===========================================================================
Name        : MiniSplit.h
Author      : Brandon Van Pelt
Description : Shadow state for the room's Della Vario (TL) mini-split.

              The unit is driven by IR, which is open loop: a remote sends its
              COMPLETE state on every press and the unit never answers. So this
              module holds what the panel believes the unit is set to, and every
              change re-sends the whole state rather than a delta.

              The transport is an IR blaster node (M5StickS3 + Grove IR unit)
              reached over ESP-NOW. That hardware is still on order, so
              transmitState() currently only logs the frame it would send —
              everything above it (the UI, the settle/coalesce logic, the saved
              state) is finished and testable without it.

              Because the panel cannot read the unit back, the shadow drifts
              whenever the handheld remote or the Della app is used. The planned
              fix is the node's IR *receiver*: decode the remote's frames and
              push the real state back here.
===========================================================================
*/
#ifndef MINISPLIT_H
#define MINISPLIT_H

#include <Arduino.h>

enum MsMode {
    MS_MODE_HEAT = 0,
    MS_MODE_COOL,
    MS_MODE_AUTO,
    MS_MODE_COUNT
};

enum MsFan {
    MS_FAN_AUTO = 0,
    MS_FAN_LOW,
    MS_FAN_MED,
    MS_FAN_HIGH,
    MS_FAN_COUNT
};

// Louvre state. Only fixed/swing are wired up: the IR protocol for this unit is
// not identified yet, and discrete blade positions (if it has them) are a
// superset of this, so the field is a uint8_t rather than a bool.
enum MsSwing {
    MS_SWING_FIXED = 0,
    MS_SWING_ON,
    MS_SWING_COUNT
};

struct MiniSplitState {
    bool    power;
    uint8_t mode;        // MsMode
    int16_t setpointF;
    uint8_t fan;         // MsFan
    uint8_t swingV;      // MsSwing — vertical blades
    uint8_t swingH;      // MsSwing — horizontal blades
};

// Setpoint limits, in F. Della's range for this series.
static const int16_t MS_MIN_F = 60;
static const int16_t MS_MAX_F = 86;

// Load the last commanded state from flash. Call once in setup().
void MINISPLIT_begin(void);

// Call every loop(): sends a coalesced frame once the user stops adjusting,
// and saves the state that was sent.
void MINISPLIT_tick(void);

MiniSplitState MINISPLIT_get(void);

void MINISPLIT_setPower(bool on);
void MINISPLIT_setMode(uint8_t mode);
void MINISPLIT_setFan(uint8_t fan);
void MINISPLIT_setSwingV(uint8_t swing);
void MINISPLIT_setSwingH(uint8_t swing);
void MINISPLIT_adjustSetpoint(int16_t deltaF);

// --- Mode as the UI presents it --------------------------------------------
// The panel shows one row of four: OFF, HEAT, COOL, AUTO. The device keeps
// power and mode as separate fields because IR protocols encode them
// separately, and because that way the running mode survives being switched
// off and comes back on its own.
#define MS_UI_MODE_COUNT (MS_MODE_COUNT + 1)

uint8_t     MINISPLIT_uiMode(void);              // 0 = off, otherwise mode + 1
void        MINISPLIT_setUiMode(uint8_t uiMode);
const char* MINISPLIT_uiModeName(uint8_t uiMode);

// True once the blaster node has been heard from. False until the hardware
// exists, so the UI can say so rather than pretending a command landed.
bool MINISPLIT_isLinked(void);

// True while a change is waiting to be sent.
bool MINISPLIT_isPending(void);

const char* MINISPLIT_modeName(uint8_t mode);
const char* MINISPLIT_fanName(uint8_t fan);

#endif // MINISPLIT_H
