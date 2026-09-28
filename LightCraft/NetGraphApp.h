/*
===========================================================================
Name        : NetGraphApp.h
Author      : Brandon Van Pelt
Description : Internet reachability over the last 24 hours, opened by the
              GRAPH button on Settings > WiFi.

              Latency is drawn as a line; any five-minute slot that saw a failed
              probe gets a full-height bar behind it, so an outage reads at a
              glance rather than having to be inferred from a gap.

              On the hidden menu, so it does not appear in the Settings list;
              Back returns to the WiFi page.
===========================================================================
*/
#ifndef NETGRAPHAPP_H
#define NETGRAPHAPP_H

#include "appConfig.h"

class Arduino_GFX;

void    netGraph_begin(Arduino_GFX* gfx);

uint8_t netGraph_createBtns(void);
void    netGraph_handler(int userInput);

// Call every loop(): redraws when a slot closes while the page is showing.
void    netGraph_tick(void);

#endif // NETGRAPHAPP_H
