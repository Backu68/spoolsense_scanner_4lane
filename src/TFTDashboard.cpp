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

void TFTDashboard::drawEmptyCell(LGFX_Sprite& canvas, int x, int y, int w, int h, bool small) {
    canvas.fillRect(x, y, w, h, COLOR_EMPTY_BG);
    canvas.setTextColor(COLOR_EMPTY_TEXT);
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(small ? 1 : 2);
    canvas.drawString("-", x + w / 2, y + h / 2);
}

void TFTDashboard::drawCell(LGFX_Sprite& canvas, int x, int y, int w, int h,
                            const TrayData& tray, bool small) {
    uint32_t bgColor = (static_cast<uint32_t>(tray.color[0]) << 16) |
                       (static_cast<uint32_t>(tray.color[1]) << 8) |
                        static_cast<uint32_t>(tray.color[2]);
    canvas.fillRect(x, y, w, h, bgColor);

    uint32_t textColor = contrastTextColor(tray.color[0], tray.color[1], tray.color[2]);
    canvas.setTextColor(textColor);
    canvas.setTextDatum(MC_DATUM);

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
        canvas.setTextSize(1);
        int cy = y + h / 2;
        canvas.drawString(label, x + w / 2, cy - 14);
        canvas.drawString(tray.material, x + w / 2, cy);
        canvas.drawString(weight, x + w / 2, cy + 14);
    } else {
        // 2x2 grid: spacious layout
        int cy = y + h / 2;
        canvas.setTextSize(1);
        canvas.drawString(label, x + w / 2, cy - 28);
        canvas.setTextSize(2);
        canvas.drawString(tray.material, x + w / 2, cy);
        canvas.setTextSize(1);
        canvas.drawString(weight, x + w / 2, cy + 28);
    }
}

void TFTDashboard::draw(LGFX_Sprite& canvas, int yOffset, const TrayDashboardState& state) {
    if (state.tray_count == 0) {
        canvas.setTextColor(COLOR_EMPTY_TEXT);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextSize(2);
        canvas.drawString("No Trays", 120, 120 - yOffset);
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
        int y = CELL_GAP + row * (cellH + CELL_GAP) - yOffset;

        if (i < state.tray_count && state.trays[i].populated) {
            drawCell(canvas, x, y, cellW, cellH, state.trays[i], small);
        } else {
            drawEmptyCell(canvas, x, y, cellW, cellH, small);
        }
    }
}
