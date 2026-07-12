#pragma once

// Pure helpers for the self-test wizard (#253): the experimental NFC stability
// score and the report redaction routines. No Arduino/FreeRTOS dependencies so
// these are unit-tested natively (test/native/test_diagnostics.cpp).

#include "DiagnosticsTypes.h"

// Worst-case severity rollup for the overall session verdict. SKIPPED/NOT_RUN
// never worsen the result; among {PASS,WARNING,FAIL} the higher wins.
DiagnosticStatus diagWorseStatus(DiagnosticStatus a, DiagnosticStatus b);

// Experimental 0-100 NFC stability score from a completed run's counters.
// Weighted deductions from 100 (detection misses dominate, then read misses,
// then UID inconsistency, retries, recoveries, latency spread). Returns 0 when
// no detection attempts were made. The weights are deliberately conservative
// and NOT frozen — validate on known-good and marginal PN5180/PN532 rigs before
// treating the number as authoritative.
uint8_t diagComputeStabilityScore(const NfcStabilityMetrics& m);

// Coarse grade band for a 0-100 score.
NfcStabilityGrade diagScoreGrade(uint8_t score);
const char* diagGradeName(NfcStabilityGrade g);

// Redact a tag UID to first-4 + last-4 hex chars, e.g. "04E9...6180".
// UIDs of 8 chars or fewer become "****". Always NUL-terminates within dst_len.
void diagRedactUid(char* dst, size_t dst_len, const char* uid);

// Strip userinfo (user:pass@) from a URL so an embedded credential never lands
// in a support report; scheme/host/path are preserved. Always NUL-terminates.
void diagRedactUrl(char* dst, size_t dst_len, const char* url);
