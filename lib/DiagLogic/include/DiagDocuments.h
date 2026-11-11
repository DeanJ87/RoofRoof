#pragma once

// The diagnostics documents (spec 017 contracts/diagnostics.md): the live
// document (GET /api/diag, /ws/diag, <base>/diag), the summary list, one move
// with its profile, and its CSV. Written from copies, outside every lock.
// The list, the move and the CSV come in parts (one summary, or
// POINTS_PER_PART points, per part) so the firmware can send them as a
// chunked reply without a large JSON pool (research R4). Unreadable values
// are null, never a made-up number (FR-004).

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "DriverLogic.h"
#include "MoveLog.h"
#include "RoofSettings.h"
#include "SummaryList.h"

namespace SQM
{
    namespace Diag
    {
        // ArduinoJson pools, sized by test_diag_documents (on the host, whose slots are bigger).
        constexpr size_t LIVE_DOC_CAPACITY = 2560;
        constexpr size_t SUMMARY_DOC_CAPACITY = 1024;
        constexpr uint8_t POINTS_PER_PART = 8;

        // The roof's part, copied under the roof's lock (RoofControl::diagLive).
        struct RoofLive
        {
            Roof::Status status;
            Roof::Output output;
            Driver::Outputs pins; // EN, RPWM and LPWM as the driver applies them
            Roof::Settings settings;
            float supplyVolts = NAN;
            float motorAmps = NAN; // the running current; NaN without current sense
            uint32_t budgetUsedMs[2] = {0, 0}; // toward each end (index: Roof::End)
            uint32_t watchdogTrips = 0;
            uint32_t maxGapMs = 0;
            uint32_t maxBeatAgeMs = 0;
            LiveLog log;
        };

        // The device's part, read on the writer's task. UNKNOWN: not available (the demo).
        struct SystemLive
        {
            uint32_t periodMs = 20;
            uint32_t watchdogLimitMs = 100;
            int64_t stepUs = UNKNOWN;
            int64_t maxStepUs = UNKNOWN;
            int64_t passMs = UNKNOWN;
            int64_t maxPassMs = UNKNOWN;
            int64_t freeHeap = UNKNOWN;
            int64_t minFreeHeap = UNKNOWN;
            int64_t largestBlock = UNKNOWN;
            int64_t stackControl = UNKNOWN;
            int64_t stackLoop = UNKNOWN;
            int64_t stackNetwork = UNKNOWN;
            int64_t rssi = UNKNOWN;
            uint32_t uptimeS = 0;
            const char *resetReason = nullptr; // null when unknown
            const char *simulation = "off";
            bool logReady = true; // false: the log's memory couldn't be allocated
        };

        // The ids of the moves still held with their profiles.
        struct HeldIds
        {
            uint32_t ids[MAX_MOVES] = {};
            uint8_t count = 0;
            bool holds(uint32_t id) const;
        };

        // {"enabled": false}: every document with the mode off (FR-005).
        void writeOff(JsonObject out);

        // Expected time to arrive from the position estimate, or UNKNOWN unless
        // the roof moves with its position known.
        int64_t remainingMs(const Roof::Status &status, const Roof::Settings &settings);

        void writeLive(JsonObject out, const RoofLive &roof, const SystemLive &system);

        // One summary. `held`: its profile is still in the log.
        void writeSummary(JsonObject out, const Summary &summary, const Clock &clock, bool held);
        std::string summaryJson(const Summary &summary, const Clock &clock, bool held);

        // What the list document shows besides the summaries.
        struct ListView
        {
            HeldIds held;       // the moves still held with their profiles
            bool keep = false;  // diag.keepSummaries
            Clock clock;
        };

        // Part `index` (from 0) of a document into `out`; false past the last part.
        bool listPart(const SummaryList &list, const ListView &view, size_t index, std::string &out);
        bool movePart(const Move &move, const Clock &clock, size_t index, std::string &out);
        bool csvPart(const Move &move, size_t index, std::string &out);
    } // namespace Diag
} // namespace SQM
