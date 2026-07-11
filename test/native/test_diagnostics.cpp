// Native unit tests for the self-test wizard pure logic (#253):
// stability score, grade banding, status rollup, and report redaction.
#include <cstdio>
#include <cstring>
#include "DiagnosticsUtil.h"

static int failures = 0;

#define CHECK(cond, name)                              \
    do {                                               \
        if (cond) { printf("  PASS  %s\n", name); }    \
        else { printf("  FAIL  %s\n", name); failures++; } \
    } while (0)

static NfcStabilityMetrics perfect() {
    NfcStabilityMetrics m{};
    m.detect_attempts = 100; m.detect_success = 100;
    m.read_attempts = 100;   m.read_success = 100;
    m.uid_mismatches = 0; m.retries = 0; m.recoveries = 0;
    m.latency_min_ms = 20; m.latency_max_ms = 22; m.latency_avg_ms = 21;
    return m;
}

int main() {
    printf("=== #253 diagnostics pure-logic ===\n");

    // --- score ---
    NfcStabilityMetrics m = perfect();
    uint8_t s = diagComputeStabilityScore(m);
    CHECK(s >= 95, "perfect run scores excellent (>=95)");
    CHECK(diagScoreGrade(s) == NfcStabilityGrade::EXCELLENT, "perfect run grades Excellent");

    NfcStabilityMetrics none{};  // all zero
    CHECK(diagComputeStabilityScore(none) == 0, "no attempts -> score 0");

    m = perfect(); m.detect_success = 0;  // every detection missed
    CHECK(diagComputeStabilityScore(m) == 0, "total detection loss scores 0");

    m = perfect(); m.detect_success = 90;  // 10% miss -> ~ -4
    s = diagComputeStabilityScore(m);
    CHECK(s >= 90 && s < 100, "10% detection miss lands high-good, not perfect");

    m = perfect(); m.read_success = 50;  // 50% read miss -> -15
    s = diagComputeStabilityScore(m);
    CHECK(s >= 80 && s <= 90, "50% read miss ~ -15");

    m = perfect(); m.recoveries = 10;  // capped at 5 -> -15
    CHECK(diagComputeStabilityScore(m) == 85, "recoveries cap at 5 (-15)");

    m = perfect(); m.uid_mismatches = 100; m.retries = 100;  // both caps hit
    s = diagComputeStabilityScore(m);
    CHECK(s == 75, "uid+retry caps (-15-10) -> 75");

    // --- grade bands ---
    CHECK(diagScoreGrade(95) == NfcStabilityGrade::EXCELLENT, "95 = Excellent");
    CHECK(diagScoreGrade(94) == NfcStabilityGrade::GOOD, "94 = Good");
    CHECK(diagScoreGrade(85) == NfcStabilityGrade::GOOD, "85 = Good");
    CHECK(diagScoreGrade(84) == NfcStabilityGrade::MARGINAL, "84 = Marginal");
    CHECK(diagScoreGrade(70) == NfcStabilityGrade::MARGINAL, "70 = Marginal");
    CHECK(diagScoreGrade(69) == NfcStabilityGrade::POOR, "69 = Poor");
    CHECK(strcmp(diagGradeName(diagScoreGrade(96)), "Excellent") == 0, "grade name Excellent");

    // --- status rollup ---
    CHECK(diagWorseStatus(DiagnosticStatus::PASS, DiagnosticStatus::FAIL) == DiagnosticStatus::FAIL,
          "FAIL beats PASS");
    CHECK(diagWorseStatus(DiagnosticStatus::PASS, DiagnosticStatus::WARNING) == DiagnosticStatus::WARNING,
          "WARNING beats PASS");
    CHECK(diagWorseStatus(DiagnosticStatus::PASS, DiagnosticStatus::SKIPPED) == DiagnosticStatus::PASS,
          "SKIPPED never worsens");
    CHECK(diagWorseStatus(DiagnosticStatus::WARNING, DiagnosticStatus::FAIL) == DiagnosticStatus::FAIL,
          "FAIL beats WARNING");

    // --- UID redaction ---
    char buf[64];
    diagRedactUid(buf, sizeof(buf), "04E9A7AD8F6180");
    CHECK(strcmp(buf, "04E9...6180") == 0, "14-char UID -> 04E9...6180");
    diagRedactUid(buf, sizeof(buf), "0412");
    CHECK(strcmp(buf, "****") == 0, "short UID fully masked");
    diagRedactUid(buf, sizeof(buf), nullptr);
    CHECK(buf[0] == '\0', "null UID -> empty");
    diagRedactUid(buf, sizeof(buf), "AABBCCDD");  // exactly 8
    CHECK(strcmp(buf, "****") == 0, "8-char UID masked (boundary)");

    // --- URL redaction ---
    diagRedactUrl(buf, sizeof(buf), "http://user:pass@192.168.1.32:7912/api/v1/info");
    CHECK(strcmp(buf, "http://192.168.1.32:7912/api/v1/info") == 0, "strips userinfo");
    diagRedactUrl(buf, sizeof(buf), "http://192.168.1.32:7912/api/v1/info");
    CHECK(strcmp(buf, "http://192.168.1.32:7912/api/v1/info") == 0, "no creds -> unchanged");
    diagRedactUrl(buf, sizeof(buf), "spoolman.local");
    CHECK(strcmp(buf, "spoolman.local") == 0, "no scheme -> verbatim");
    diagRedactUrl(buf, sizeof(buf), "https://token:x@host/path?a=b@c");
    CHECK(strcmp(buf, "https://host/path?a=b@c") == 0, "only authority '@' stripped, not path '@'");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
