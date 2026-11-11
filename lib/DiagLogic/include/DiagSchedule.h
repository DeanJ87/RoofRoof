#pragma once

// When diagnostics go out (spec 017 FR-006, FR-018; research R9, R10): the
// stream's interval, and MQTT's live document and move summaries. Nothing is
// due over MQTT while the roof is busy (moving, a close waiting, the
// automatic close attempting), so spec 008's busy budget is never used.

#include <cstdint>

#include "SummaryList.h"

namespace SQM
{
    namespace Diag
    {
        constexpr uint32_t STREAM_MOVING_MS = 250;
        constexpr uint32_t STREAM_REST_MS = 2000;
        constexpr uint32_t MQTT_LIVE_MS = 60000;
        constexpr const char *MQTT_LIVE_SUBTOPIC = "diag";
        constexpr const char *MQTT_MOVE_SUBTOPIC = "diag/move";

        // The stream's interval for the roof's state now.
        uint32_t streamIntervalMs(Roof::State state);

        class MqttSchedule
        {
        public:
            // Summaries up to `newestId` are never sent (those before MQTT
            // diagnostics started, and every kept one).
            void start(uint32_t newestId) { publishedId = newestId; }
            bool liveDue(uint32_t nowMs, bool busy) const;
            // A failed publish counts too: tried again after the interval.
            void livePublished(uint32_t nowMs);
            // The list index of the oldest finished summary not yet published,
            // or -1 (none, or busy).
            int nextSummary(const SummaryList &list, bool busy) const;
            void summaryPublished(uint32_t id) { publishedId = id; }

        private:
            bool liveAny = false;
            uint32_t lastLiveMs = 0;
            uint32_t publishedId = 0;
        };
    } // namespace Diag
} // namespace SQM
