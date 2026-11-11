#pragma once

// Delivery of the close sequence's critical alerts (spec 005): an alert
// counts as sent only once a channel has taken it. One that no channel took
// (WiFi down at boot, a full queue, a failed send) is sent again, at most
// every ALERT_RETRY_MS, until one does or the sequence has nothing left to
// report. Pure: the caller asks the dispatcher how the last send went.

#include <cstdint>

namespace SQM
{
    namespace CloseSeq
    {
        // The gap between two sends of an undelivered alert.
        constexpr uint64_t ALERT_RETRY_MS = 60000;

        // How the last send went, as the dispatcher sees it.
        enum class Delivery : uint8_t
        {
            Pending,   // a channel is still trying
            Delivered, // at least one channel took it
            Failed,    // every channel that is on failed or was skipped; or it wasn't stored
            NoChannel, // no channel is switched on: nothing to retry
        };

        class AlertRetry
        {
        public:
            // A critical alert was handed to the dispatcher at `nowMs`.
            void sent(uint64_t nowMs);
            // Whether to send the same alert again now.
            bool resendDue(Delivery delivery, uint64_t nowMs);
            // Nothing left to deliver (the sequence is idle).
            void clear() { waiting = false; }
            bool active() const { return waiting; }

        private:
            bool waiting = false;
            uint64_t lastSendMs = 0;
        };
    } // namespace CloseSeq
} // namespace SQM
