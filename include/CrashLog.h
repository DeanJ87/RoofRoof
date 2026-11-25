#pragma once

// Crash capture (spec 029): the RTC memory that survives a restart (the
// panic's capture, the main loop's snapshot, the log ring), the panic and watchdog hooks, and the crash records in NVS. The
// records, ring, checks and documents are lib/CrashLogic's; this file only
// reads the hardware and NVS.

#include <cstdarg>
#include <cstdint>
#include <string>

#include "ChunkText.h"
#include "CrashDocuments.h"

namespace SQM
{
    namespace CrashLog
    {
        // First thing in setup(): keeps or empties the log ring by the reset
        // reason, sets aside the last capture and snapshot, and installs the
        // panic, task watchdog and ESP-IDF log hooks.
        void arm();

        // After the motor driver's EN is low and before the roof's task
        // starts (FR-009): the restart, if unexpected, becomes a record in NVS.
        void keepRecord();

        // From the main loop: the context snapshot, at most once a second.
        void snapshot(const char *roofState, uint32_t roofStackFree);

        // From the logger: one printed line into the ring.
        void logLine(uint8_t level, const char *tag, const char *format, va_list args);

        // GET /api/diag/log: the ring's lines as text, oldest first (from a copy taken under its lock).
        void logText(HeapSafe::ChunkText &out);

        // GET /api/diag/crashes: the kept records, newest first (read from NVS).
        void crashesText(HeapSafe::ChunkText &out);

        // This start as the documents show it.
        Crash::BootView bootView();
    } // namespace CrashLog
} // namespace SQM
