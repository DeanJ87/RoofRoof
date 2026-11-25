#pragma once

// The crash documents (spec 029 contracts/crashes.md): GET /api/diag/crashes
// and GET /api/diag/log, both plain text, one fact per line. Text rather than
// JSON: the image is near its OTA slot's limit (research R9), and both are read
// by a person, a copy-paste and tools/decode_crash.py.

#include <cstddef>
#include <cstdint>
#include <string>

#include "CrashRecord.h"
#include "LogRing.h"

namespace SQM
{
    namespace Crash
    {
        // This start, as the documents show it.
        struct BootView
        {
            uint8_t resetCode = 0;
            uint32_t bootCount = 0;
            const char *firmware = "";
            const char *build = "";
        };

        // Appends one record, a line per fact, starting "Record <seq>: <kind>".
        void writeRecord(std::string &out, const Record &record);

        // The records document: this start, then each record after a blank line; `records` newest first.
        std::string crashesText(const BootView &boot, const Record *records, size_t count);

        // The log as text, oldest first: "#<boot> <seconds>s <LEVEL> <text>" per line.
        std::string logText(const LogLine *lines, size_t count);
    } // namespace Crash
} // namespace SQM
