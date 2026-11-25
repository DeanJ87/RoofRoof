#pragma once

// One unexpected restart as kept in NVS (spec 029 FR-007, FR-008): built at
// the next start from the reset reason, the panic's capture and the last
// snapshot (each only when intact) and the log lines before the restart.

#include <cstddef>
#include <cstdint>
#include <string>

#include "CrashTypes.h"
#include "LogRing.h"

namespace SQM
{
    namespace Crash
    {
        constexpr uint8_t RECORD_VERSION = 1;
        constexpr size_t KEEP = 3;         // records kept in NVS
        constexpr size_t RECORD_LINES = 8; // log lines per record
        constexpr size_t FIRMWARE_CHARS = 24;
        constexpr size_t BUILD_CHARS = 17; // 16 hex digits of the .elf's SHA-256

        // A log line in a record (the boot is always the one that ended).
        struct RecordLine
        {
            uint32_t ms;
            uint8_t level;
            char text[LINE_CHARS];
            uint8_t pad[3];
        };

        // The capture and the snapshot are kept as they were read (when intact),
        // so a record is a few copies, not a field-by-field translation.
        struct Record
        {
            static constexpr uint8_t CAPTURED = 1u << 0; // `capture` was intact
            static constexpr uint8_t SNAPSHOT = 1u << 1; // `snapshot` was intact
            static constexpr uint8_t TIMED = 1u << 2;    // `epoch` is known
            uint8_t version;
            uint8_t kind;
            uint8_t resetCode;
            uint8_t flags;
            uint32_t seq;
            uint32_t uptimeMs; // the capture's, else the snapshot's
            uint32_t epoch;
            uint8_t logCount;
            uint8_t pad[3];
            char firmware[FIRMWARE_CHARS];
            char build[BUILD_CHARS];
            uint8_t pad2[3];
            Capture capture;   // valid with CAPTURED (its frames at most MAX_FRAMES)
            Snapshot snapshot; // valid with SNAPSHOT
            RecordLine log[RECORD_LINES];
        };

        // What a record is built from. A capture or snapshot that isn't intact is passed as null.
        struct RecordInputs
        {
            ResetCode reset = ResetCode::Unknown;
            const Capture *capture = nullptr;
            const Snapshot *snapshot = nullptr;
            const char *firmware = "";
            const char *build = "";
            uint32_t seq = 0;
            const LogLine *log = nullptr; // the tail before this start, oldest first
            size_t logCount = 0;
        };

        // The kind of restart a reset code is (FR-007, FR-010).
        Kind kindOf(ResetCode code);
        const char *kindName(Kind kind); // "none", "panic", "watchdog", "brownout"

        // SQMeter spec 038's names: "poweron", "panic", "watchdog", "brownout", ...
        const char *resetToken(uint8_t code);

        // Fills `out`; false (and `out` untouched) when the reset makes no record.
        bool build(const RecordInputs &in, Record &out);

        // A stored blob as a record: false for another size, version or kind.
        bool decode(const void *bytes, size_t length, Record &out);

        // The NVS slot a sequence number goes to: the newest KEEP survive.
        constexpr size_t slot(uint32_t seq)
        {
            return seq % KEEP;
        }
    } // namespace Crash
} // namespace SQM
