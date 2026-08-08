#pragma once
#include <LovyanGFX.hpp>
#include "TrayDashboardTypes.h"

class TFTDashboard {
public:
    // Draw the 240x240 tray grid into `canvas`, shifting all Y by -yOffset —
    // same body contract as TFTManager's draw*240 functions. The caller's
    // backend owns background fill and pushing to the panel.
    void draw(LGFX_Sprite& canvas, int yOffset, const TrayDashboardState& state);

private:
    void drawCell(LGFX_Sprite& canvas, int x, int y, int w, int h,
                  const TrayData& tray, bool small);
    void drawEmptyCell(LGFX_Sprite& canvas, int x, int y, int w, int h, bool small);
    uint32_t contrastTextColor(uint8_t r, uint8_t g, uint8_t b);
};
