#pragma once
#include <LovyanGFX.hpp>
#include "TrayDashboardTypes.h"

class TFTDashboard {
public:
    void render(LGFX_Sprite* sprite, const TrayDashboardState& state, int offX = 0, int offY = 0);

private:
    void renderCell(LGFX_Sprite* sprite, int x, int y, int w, int h,
                    const TrayData& tray, bool small);
    void renderEmptyCell(LGFX_Sprite* sprite, int x, int y, int w, int h, bool small);
    uint32_t contrastTextColor(uint8_t r, uint8_t g, uint8_t b);
};
