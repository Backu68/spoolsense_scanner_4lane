#include "TFTDashboard.h"

static const uint32_t COLOR_EMPTY_BG = 0x1A1A1A;
static const uint32_t COLOR_EMPTY_TEXT = 0x555555;
static const uint32_t COLOR_WHITE = 0xFFFFFF;
static const uint32_t COLOR_BLACK = 0x000000;
static const int CELL_GAP = 2;

uint32_t TFTDashboard::contrastTextColor(uint8_t r, uint8_t g, uint8_t b) {
    float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
    return luminance > 128.0f ? COLOR_BLACK : COLOR_WHITE;
}

void TFTDashboard::renderEmptyCell(LGFX_Sprite* sprite, int x, int y, int w, int h, bool small) {
    sprite->fillRect(x, y, w, h, COLOR_EMPTY_BG);
    sprite->setTextColor(COLOR_EMPTY_TEXT);
    sprite->setTextDatum(MC_DATUM);
    sprite->setTextSize(small ? 1 : 2);
    sprite->drawString("-", x + w / 2, y + h / 2);
}

void TFTDashboard::renderCell(LGFX_Sprite* sprite, int x, int y, int w, int h,
                               const TrayData& tray, bool small) {
    uint32_t bgColor = (static_cast<uint32_t>(tray.color[0]) << 16) |
                       (static_cast<uint32_t>(tray.color[1]) << 8) |
                        static_cast<uint32_t>(tray.color[2]);
    sprite->fillRect(x, y, w, h, bgColor);

    uint32_t textColor = contrastTextColor(tray.color[0], tray.color[1], tray.color[2]);
    sprite->setTextColor(textColor);
    sprite->setTextDatum(MC_DATUM);

    char label[6];
    snprintf(label, sizeof(label), "T%d", tray.tray_index + 1);

    char weight[8];
    if (tray.weight_g > 0) {
        snprintf(weight, sizeof(weight), "%dg", tray.weight_g);
    } else {
        snprintf(weight, sizeof(weight), "?g");
    }

    if (small) {
        // 4x4 grid: compact layout
        sprite->setTextSize(1);
        int cy = y + h / 2;
        sprite->drawString(label, x + w / 2, cy - 14);
        sprite->drawString(tray.material, x + w / 2, cy);
        sprite->drawString(weight, x + w / 2, cy + 14);
    } else {
        // 2x2 grid: spacious layout
        int cy = y + h / 2;
        sprite->setTextSize(1);
        sprite->drawString(label, x + w / 2, cy - 28);
        sprite->setTextSize(2);
        sprite->drawString(tray.material, x + w / 2, cy);
        sprite->setTextSize(1);
        sprite->drawString(weight, x + w / 2, cy + 28);
    }
}

void TFTDashboard::render(LGFX_Sprite* sprite, const TrayDashboardState& state, int offX, int offY) {
    sprite->fillScreen(0x000000);

    if (state.tray_count == 0) {
        sprite->setTextColor(COLOR_EMPTY_TEXT);
        sprite->setTextDatum(MC_DATUM);
        sprite->setTextSize(2);
        sprite->drawString("No Trays", 120, 120);
        sprite->pushSprite(offX, offY);
        return;
    }

    // Determine grid dimensions
    uint8_t cols, rows;
    bool small;
    if (state.tray_count <= 4) {
        cols = 2; rows = 2; small = false;
    } else if (state.tray_count <= 8) {
        cols = 2; rows = 4; small = true;
    } else {
        cols = 4; rows = 4; small = true;
    }

    int cellW = (240 - (cols + 1) * CELL_GAP) / cols;
    int cellH = (240 - (rows + 1) * CELL_GAP) / rows;

    for (uint8_t i = 0; i < rows * cols; i++) {
        uint8_t col = i % cols;
        uint8_t row = i / cols;
        int x = CELL_GAP + col * (cellW + CELL_GAP);
        int y = CELL_GAP + row * (cellH + CELL_GAP);

        if (i < state.tray_count && state.trays[i].populated) {
            renderCell(sprite, x, y, cellW, cellH, state.trays[i], small);
        } else {
            renderEmptyCell(sprite, x, y, cellW, cellH, small);
        }
    }

    sprite->pushSprite(offX, offY);
}
