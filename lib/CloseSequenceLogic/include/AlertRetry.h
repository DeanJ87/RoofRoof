#pragma once

// Delivery of the close sequence's critical alerts (spec 005): an alert
// counts as sent only once a channel has taken it. One that no channel took
// (WiFi down at boot, a full queue, a failed send) is sent again, first after
// ALERT_RETRY_MS and then at doubling gaps up to ALERT_RETRY_MAX_MS, until one
// does or the sequence has nothing left to report. A send still pending after
// ALERT_PENDING_STALE_MS counts as failed (a delivery that never reports back).
// Pure: the caller asks the dispatcher how the last send went.

#include <cstdint>

namespace SQM
{
    namespace CloseSeq
    {
        // The first gap before an undelivered alert is sent again; it doubles after each resend...
        constexpr uint64_t ALERT_RETRY_MS = 60000;
        // ...up to the reminder interval (spec 014: the policy's remindMs, see setMaxGap).
        constexpr uint64_t ALERT_RETRY_MAX_MS = 1800000;
        // A send still pending this long counts as failed.
        constexpr uint64_t ALERT_PENDING_STALE_MS = 120000;

        // How the last send went, as the dispatcher sees it.
        enum class Delivery : uint8_t
        {
            Pending,   // a channel is still trying
            Delivered, // at least one channel took it
            Failed,    // every channel that could take it failed; or it wasn't stored
            NoChannel, // no channel is switched on and able to send: nothing to retry
        };

        class AlertRetry
        {
        public:
            // A new critical alert was handed to the dispatcher at `nowMs`.
            void sent(uint64_t nowMs);
            // The same alert was sent again at `nowMs` (the next gap doubles).
            void resent(uint64_t nowMs);
            // Whether to send the same alert again now.
            bool resendDue(Delivery delivery, uint64_t nowMs);
            // Nothing left to deliver (the sequence is idle).
            void clear() { waiting = false; }
            bool active() const { return waiting; }
            uint64_t gapMs() const { return gap; }
            // Spec 014: the longest gap, the policy's reminder interval (never
            // below the first gap); applies from the next resend.
            void setMaxGap(uint64_t ms) { maxGap = ms < ALERT_RETRY_MS ? ALERT_RETRY_MS : ms; }

        private:
            bool waiting = false;
            uint64_t lastSendMs = 0;
            uint64_t gap = ALERT_RETRY_MS;
            uint64_t maxGap = ALERT_RETRY_MAX_MS;
        };
    } // namespace CloseSeq
} // namespace SQM
