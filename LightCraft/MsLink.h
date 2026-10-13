/*
===========================================================================
Name        : MsLink.h
Author      : Brandon Van Pelt
Description : Wire protocol between the LightCraft panel and the IR blaster
              node (M5StickS3), carried over ESP-NOW.

              !! TWO COPIES OF THIS FILE EXIST AND MUST STAY IDENTICAL !!
                 LightCraft/MsLink.h               (panel)
                 IRNode/MiniSplitNode/MsLink.h     (node)
              Arduino only includes headers from the sketch folder, so this is
              duplicated rather than shared. Change one, change both, and bump
              MSLINK_VERSION if the layout moves.

              DESIGN NOTES

              Broadcast, not unicast. With exactly two devices and a handful
              of packets a minute there is nothing to gain from pairing, and
              broadcast means neither side has to know the other's MAC -- so
              replacing either board needs no reconfiguration.

              No ESP-NOW encryption and no reliance on its delivery callback.
              Delivery is confirmed at the application layer with MSLINK_ACK,
              which reports the thing we actually care about: that the IR
              frame was transmitted, not merely that a radio packet left.

              THE PANEL NEVER PERIODICALLY ASSERTS STATE. This is deliberate
              and was learned the hard way. The panel cannot read the unit, so
              anything it re-sends on a timer is really "override whoever last
              touched the AC" -- which fought the handheld remote and the
              Della phone app. So:
                - MSLINK_CMD goes out only on a real change, plus a few
                  bounded retries until the node acks it, then it stops
                - MSLINK_PING carries no state at all. It exists purely so the
                  node can find and hold the panel's radio channel, which it
                  otherwise could not do (it has no WiFi credentials and the
                  channel is whatever the router picked).

              `seq` increments only on a real commanded change, and the node
              transmits IR only when seq differs from the last it applied.
              Retries therefore repeat the SAME seq and are idempotent: a node
              that already applied it ignores the repeat. The test is != rather
              than >, so either side rebooting and restarting its counter still
              converges instead of deadlocking on a stale high-water mark.

              A consequence worth knowing: the panel's state is not pushed to a
              freshly booted node, so the node shows its own defaults until the
              next real command. That is correct -- the AC's settings did not
              change just because the node rebooted, and asserting on boot
              would be another unprompted override.
===========================================================================
*/
#ifndef MSLINK_H
#define MSLINK_H

#include <stdint.h>

static const uint8_t MSLINK_MAGIC   = 0x4D;   // 'M'

// v2: added MSLINK_PING and the DRY/FAN modes, and the panel stopped
// periodically asserting state. The packet LAYOUT is unchanged, so a mismatch
// would otherwise half-work in a confusing way -- a v2 panel's pings would be
// dropped by a v1 node, which would then never lock onto the channel. Bumping
// makes a mismatch show up honestly as "no link" instead. Flash both sides.
static const uint8_t MSLINK_VERSION = 2;

enum MsLinkType : uint8_t {
    MSLINK_CMD    = 1,   // panel -> node: desired state + seq. SENT ONLY ON A
                         //   REAL CHANGE, plus a few bounded retries until the
                         //   node acks. The panel never periodically asserts
                         //   state -- see the note below.
    MSLINK_ACK    = 2,   // node  -> panel: this seq was applied and sent over IR
    MSLINK_RESYNC = 3,   // node  -> panel: state observed from the handheld remote
    MSLINK_PING   = 4    // panel -> node: liveness + channel beacon. Carries NO
                         //   state and no command. The node uses it only to
                         //   find and hold the panel's radio channel.
};

// These mirror MiniSplit.h's MsMode / MsFan / MsSwing. The node does not
// include MiniSplit.h, so the numbering is restated here; the panel side
// static_asserts the two agree so drift cannot go unnoticed.
enum MsLinkMode  : uint8_t { MSLINK_MODE_HEAT = 0, MSLINK_MODE_COOL = 1, MSLINK_MODE_AUTO = 2,
                             MSLINK_MODE_DRY  = 3, MSLINK_MODE_FAN  = 4 };
enum MsLinkFan   : uint8_t { MSLINK_FAN_AUTO = 0, MSLINK_FAN_LOW = 1, MSLINK_FAN_MED = 2, MSLINK_FAN_HIGH = 3 };
enum MsLinkSwing : uint8_t { MSLINK_SWING_FIXED = 0, MSLINK_SWING_ON = 1 };

struct __attribute__((packed)) MsLinkState {
    uint8_t power;
    uint8_t mode;         // MsLinkMode
    int16_t setpointF;
    uint8_t fan;          // MsLinkFan
    uint8_t swingV;       // MsLinkSwing
    uint8_t swingH;       // MsLinkSwing
};

struct __attribute__((packed)) MsLinkPacket {
    uint8_t     magic;
    uint8_t     version;
    uint8_t     type;     // MsLinkType
    uint8_t     flags;    // reserved, send 0
    uint32_t    seq;
    MsLinkState state;
};

static const uint8_t MSLINK_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Panel ping cadence, and how long either side waits before calling the link
// dead. The timeout is a few pings so one lost packet is not a visible outage.
static const uint32_t MSLINK_PING_MS         = 2000;
static const uint32_t MSLINK_LINK_TIMEOUT_MS = 8000;

// Bounded command retry. Repeats the same seq until the node acks, then stops
// -- this replaces the old "heartbeat carries the state forever" behaviour.
static const uint32_t MSLINK_CMD_RETRY_MS   = 700;
static const uint8_t  MSLINK_CMD_MAX_TRIES  = 5;

static inline bool MSLINK_valid(const uint8_t* data, int len)
{
    if (data == nullptr || len != (int)sizeof(MsLinkPacket))
        return false;

    const MsLinkPacket* p = (const MsLinkPacket*)data;
    return (p->magic == MSLINK_MAGIC) && (p->version == MSLINK_VERSION);
}

static inline bool MSLINK_sameState(const MsLinkState& a, const MsLinkState& b)
{
    return a.power     == b.power
        && a.mode      == b.mode
        && a.setpointF == b.setpointF
        && a.fan       == b.fan
        && a.swingV    == b.swingV
        && a.swingH    == b.swingH;
}

#endif // MSLINK_H
