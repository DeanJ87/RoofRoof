#pragma once

// Spec 030 (FR-008): a critical alert waiting for a channel, as kept in NVS
// so a restart or a power cut doesn't lose it (one record per critical
// event). The record starts with a version byte and ends with a checksum: a
// record that doesn't check out is dropped, never sent half-read.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AlertEngine.h"

namespace SQM
{
    namespace CloseSeq
    {
        constexpr uint8_t KEPT_ALERT_VERSION = 1;
        // Each text is cut to this many bytes; a record is never longer than
        // KEPT_ALERT_MAX_BYTES (an NVS blob, written a few times per incident).
        constexpr size_t KEPT_ALERT_MAX_TEXT = 1024;
        constexpr size_t KEPT_ALERT_MAX_VARS = 8;
        constexpr size_t KEPT_ALERT_MAX_BYTES = 4096;

        struct KeptAlert
        {
            Alerts::Alert alert;   // type, level, title, message and variables
            uint32_t queuedAt = 0; // epoch seconds when it was queued (0: the clock wasn't set)
        };

        std::vector<uint8_t> encodeKept(const KeptAlert &kept);
        // False when the record is empty, of another version, cut short or doesn't check out.
        bool decodeKept(const uint8_t *data, size_t length, KeptAlert &out);

        // A kept alert sent again after a restart says so: "Sent before a restart
        // at <when>." (`when` is the local time it was queued, or "" without a clock).
        Alerts::Alert markRestarted(Alerts::Alert alert, const std::string &when);
    } // namespace CloseSeq
} // namespace SQM
