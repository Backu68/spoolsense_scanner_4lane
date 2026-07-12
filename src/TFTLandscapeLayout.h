#pragma once
#include <stdint.h>

// Pure geometry for the ILI9488 landscape scanned-spool layout. No LovyanGFX
// dependency so it is unit-testable natively. All values are in canvas pixels
// for a (w x h) surface (nominally 480x320).
struct LandscapeRect { int x, y, w, h; };

struct LandscapeLayout {
    int          headerH;        // header bar height
    LandscapeRect spool;         // bounding box of the spool disc (square)
    int          spoolCx, spoolCy, spoolOuterR, spoolInnerR;
    int          textX;          // left edge of the right-hand text column
    int          brandY, materialY, weightY;  // text baselines (MC_DATUM)
    LandscapeRect weightBar;      // weight bar rect
};

// Compute the layout for a canvas of size (w x h).
LandscapeLayout landscapeLayout(int w, int h);

// Filled width in pixels of a weight bar of width barW for remaining/total.
int landscapeBarFill(int barW, float remaining, float total);
