#pragma once

#include <cstring>

// Decide whether a queued write may proceed against the tag currently selected
// on the reader.
//
//   expectedUid     — UID the write was bound to when it was enqueued.
//                     Empty or null means "unbound": legacy behaviour, accept
//                     whatever tag is present.
//   selectedUidHex  — UID hex string of the tag selected on the reader during
//                     this scan cycle (uppercase, no separators).
//
// Bound writes fail closed: if no tag is selected we reject rather than guess.
// The comparison is exact — matching on a prefix is what allowed #276, where
// the scan-cooldown dedup let a same-batch tag sharing three UID bytes pass a
// check that had been made against the previous tag.
inline bool writeUidMatches(const char* expectedUid, const char* selectedUidHex) {
    if (expectedUid == nullptr || expectedUid[0] == '\0') {
        return true;   // unbound write — accept whatever is present
    }
    if (selectedUidHex == nullptr || selectedUidHex[0] == '\0') {
        return false;  // bound write with no selected tag — fail closed
    }
    return strcmp(expectedUid, selectedUidHex) == 0;
}
