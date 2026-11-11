#pragma once

// The kept move summaries (spec 017 FR-016, FR-017; research R11): one file,
// /diag-moves.bin, written whole. A 16-byte header (magic "RRDG", version,
// count, the next move id, a CRC-32 of the records) and up to MAX_SUMMARIES
// records of KEPT_RECORD_BYTES, little-endian, newest first. KeptWrite
// decides when a write is due; the platform check (the roof at rest, no
// close waiting, the sequence idle) is the firmware's flash-write guard.

#include <cstddef>
#include <cstdint>

#include "SummaryList.h"

namespace SQM
{
    namespace Diag
    {
        constexpr const char *KEPT_PATH = "/diag-moves.bin";
        constexpr uint8_t KEPT_VERSION = 1;
        constexpr size_t KEPT_HEADER_BYTES = 16;
        constexpr size_t KEPT_RECORD_BYTES = 56;
        constexpr size_t KEPT_MAX_BYTES = KEPT_HEADER_BYTES + MAX_SUMMARIES * KEPT_RECORD_BYTES;
        // The least time between two writes (at most 12 an hour).
        constexpr uint32_t KEPT_WRITE_MIN_MS = 300000;

        // The list as a file into `out` (at least KEPT_MAX_BYTES); returns the
        // bytes written. Summaries not yet kept get their epochs from `clock`.
        size_t encodeKept(const SummaryList &list, uint32_t nextId, const Clock &clock, uint8_t *out, size_t capacity);
        // A file back into `out` (every summary marked kept) and the next id
        // (at least one past the highest kept id). False, with `out` unchanged,
        // for anything that doesn't match: magic, version, size, count or CRC.
        bool decodeKept(const uint8_t *data, size_t size, SummaryList &out, uint32_t &nextId);

        // When the file is due to be written.
        class KeptWrite
        {
        public:
            // The list as it stands needs no write (it was just loaded).
            void start(uint32_t revision) { writtenRevision = revision; }
            // After a clear: at once, setting on or off (the file is emptied or
            // removed). Otherwise the setting on, the list changed since the last
            // write, and no write in the last KEPT_WRITE_MIN_MS (or none yet).
            bool due(uint32_t nowMs, uint32_t revision, bool settingOn) const;
            // Written (or the write failed: tried again after the interval).
            void written(uint32_t nowMs, uint32_t revision);
            // A person cleared the log: the empty list is due without the interval.
            void cleared() { urgent = true; }

        private:
            bool any = false;
            bool urgent = false;
            uint32_t lastMs = 0;
            uint32_t writtenRevision = 0;
        };
    } // namespace Diag
} // namespace SQM
