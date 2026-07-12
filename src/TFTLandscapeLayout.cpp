#include "TFTLandscapeLayout.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

LandscapeLayout landscapeLayout(int w, int h) {
    LandscapeLayout L{};
    L.headerH = 26;

    // Left half hosts the spool disc, vertically centered below the header.
    int leftW  = w / 2;
    int availH = h - L.headerH;
    L.spoolOuterR = clampi((leftW < availH ? leftW : availH) / 2 - 12, 20, 140);
    L.spoolInnerR = L.spoolOuterR * 3 / 8;
    L.spoolCx = leftW / 2;
    L.spoolCy = L.headerH + availH / 2;
    L.spool = { L.spoolCx - L.spoolOuterR, L.spoolCy - L.spoolOuterR,
                L.spoolOuterR * 2, L.spoolOuterR * 2 };

    // Right half hosts the text column + weight bar.
    L.textX = leftW + 16;
    int col0 = L.headerH + (availH - 96) / 2;   // stack of ~96px
    L.brandY    = col0 + 12;
    L.materialY = col0 + 40;
    L.weightY   = col0 + 68;
    L.weightBar = { L.textX, col0 + 84, w - L.textX - 16, 12 };
    return L;
}

int landscapeBarFill(int barW, float remaining, float total) {
    if (total <= 0.0f) return 0;
    float r = remaining / total;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    return (int)(barW * r);
}
