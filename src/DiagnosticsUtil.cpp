#include "DiagnosticsUtil.h"

#include <stdio.h>
#include <string.h>

static uint8_t severityRank(DiagnosticStatus s) {
    switch (s) {
        case DiagnosticStatus::FAIL:    return 3;
        case DiagnosticStatus::WARNING: return 2;
        case DiagnosticStatus::PASS:    return 1;
        default:                        return 0;  // NOT_RUN/RUNNING/WAITING/SKIPPED
    }
}

DiagnosticStatus diagWorseStatus(DiagnosticStatus a, DiagnosticStatus b) {
    return severityRank(a) >= severityRank(b) ? a : b;
}

// Deduct-from-100 model. Each term is bounded so a single dimension can't
// underflow the score on its own, and the caps sum past 100 so a genuinely
// broken rig lands at 0.
uint8_t diagComputeStabilityScore(const NfcStabilityMetrics& m) {
    if (m.detect_attempts == 0) {
        return 0;
    }

    uint32_t penalty = 0;

    // Detection misses dominate (weight 40).
    uint32_t detect_miss = (m.detect_success >= m.detect_attempts)
                               ? 0
                               : (uint32_t)(m.detect_attempts - m.detect_success);
    penalty += (detect_miss * 40u) / m.detect_attempts;

    // Read misses (weight 30), only if reads were attempted.
    if (m.read_attempts > 0) {
        uint32_t read_miss = (m.read_success >= m.read_attempts)
                                 ? 0
                                 : (uint32_t)(m.read_attempts - m.read_success);
        penalty += (read_miss * 30u) / m.read_attempts;
    }

    // UID inconsistency (weight 15, capped).
    uint32_t uid_pen = (m.uid_mismatches * 15u) / m.detect_attempts;
    penalty += uid_pen > 15u ? 15u : uid_pen;

    // Retry rate (weight 10, capped).
    uint32_t retry_pen = (m.retries * 10u) / m.detect_attempts;
    penalty += retry_pen > 10u ? 10u : retry_pen;

    // Recoveries: 3 each, capped at 15.
    uint32_t rec = m.recoveries > 5u ? 5u : m.recoveries;
    penalty += rec * 3u;

    // Latency spread (weight 5, capped): jittery latency is a mild negative.
    if (m.latency_max_ms > 0 && m.latency_max_ms >= m.latency_min_ms) {
        uint32_t spread = ((m.latency_max_ms - m.latency_min_ms) * 5u) / m.latency_max_ms;
        penalty += spread > 5u ? 5u : spread;
    }

    if (penalty >= 100u) {
        return 0;
    }
    return (uint8_t)(100u - penalty);
}

NfcStabilityGrade diagScoreGrade(uint8_t score) {
    if (score >= 95) return NfcStabilityGrade::EXCELLENT;
    if (score >= 85) return NfcStabilityGrade::GOOD;
    if (score >= 70) return NfcStabilityGrade::MARGINAL;
    return NfcStabilityGrade::POOR;
}

const char* diagGradeName(NfcStabilityGrade g) {
    switch (g) {
        case NfcStabilityGrade::EXCELLENT: return "Excellent";
        case NfcStabilityGrade::GOOD:      return "Good";
        case NfcStabilityGrade::MARGINAL:  return "Marginal";
        default:                           return "Poor";
    }
}

void diagRedactUid(char* dst, size_t dst_len, const char* uid) {
    if (dst_len == 0) return;
    if (!uid) { dst[0] = '\0'; return; }

    size_t n = strlen(uid);
    if (n <= 8) {
        // Too short to partially reveal without exposing most of it.
        snprintf(dst, dst_len, "****");
        return;
    }
    // first 4 + "..." + last 4
    snprintf(dst, dst_len, "%.4s...%s", uid, uid + (n - 4));
}

void diagRedactUrl(char* dst, size_t dst_len, const char* url) {
    if (dst_len == 0) return;
    if (!url) { dst[0] = '\0'; return; }

    const char* scheme_end = strstr(url, "://");
    if (!scheme_end) {
        // No scheme — copy verbatim (bounded).
        snprintf(dst, dst_len, "%s", url);
        return;
    }
    const char* authority = scheme_end + 3;

    // userinfo, if present, ends at '@' before the next '/'.
    const char* at = strchr(authority, '@');
    const char* slash = strchr(authority, '/');
    if (at && (!slash || at < slash)) {
        // Emit scheme://<host-onward>, dropping user:pass@.
        size_t scheme_len = (size_t)(authority - url);  // includes "://"
        if (scheme_len >= dst_len) scheme_len = dst_len - 1;
        memcpy(dst, url, scheme_len);
        dst[scheme_len] = '\0';
        // append everything after '@'
        size_t used = strlen(dst);
        snprintf(dst + used, dst_len - used, "%s", at + 1);
        return;
    }

    // No credentials to strip.
    snprintf(dst, dst_len, "%s", url);
}
