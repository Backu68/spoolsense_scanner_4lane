#include "TFTLandscapeLayout.h"
#include <cassert>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } else { printf("  PASS %s\n", msg); } } while(0)

int main() {
    printf("=== Landscape layout tests ===\n");
    LandscapeLayout L = landscapeLayout(480, 320);

    // Spool disc sits in the left half and is fully on-canvas
    CHECK(L.spoolCx < 240, "spool center in left half");
    CHECK(L.spoolCx - L.spoolOuterR >= 0, "spool left edge on-canvas");
    CHECK(L.spoolCy + L.spoolOuterR <= 320, "spool bottom on-canvas");
    CHECK(L.spoolInnerR > 0 && L.spoolInnerR < L.spoolOuterR, "inner < outer radius");

    // Text column starts in the right half
    CHECK(L.textX >= 240, "text column in right half");

    // Weight bar within the right column and on-canvas
    CHECK(L.weightBar.x >= 240, "bar starts in right half");
    CHECK(L.weightBar.x + L.weightBar.w <= 480, "bar right edge on-canvas");
    CHECK(L.headerH > 0 && L.headerH < 60, "header height sane");

    // Bar fill math
    CHECK(landscapeBarFill(200, 1000, 1000) == 200, "full bar");
    CHECK(landscapeBarFill(200, 0, 1000) == 0, "empty bar");
    CHECK(landscapeBarFill(200, 500, 1000) == 100, "half bar");
    CHECK(landscapeBarFill(200, 1500, 1000) == 200, "overfull clamps to full");
    CHECK(landscapeBarFill(200, 100, 0) == 0, "zero total -> empty");

    printf(failures ? "\nFAILURES: %d\n" : "\nOK: 0 failure(s)\n", failures);
    return failures ? 1 : 0;
}
