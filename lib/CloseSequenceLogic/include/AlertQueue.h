#pragma once

// Spec 030 (FR-008): alerts survive outages. Every alert the alert engine,
// the close sequence and the needs-a-person watcher raise waits here until a
// channel takes it: sent at once while the network is up, then again after
// AlertRetry's back-off (60 s doubling up to 30 min) while no channel took it,
// and at once when the network comes back. It leaves when a channel took it,
// when no channel can ever take it (none switched on), when a newer alert of
// the same event supersedes it, or when its condition is resolved. Bounded:
// when full, the oldest non-critical alert makes room. Pure: the caller sends
// and asks how each send went.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "AlertEngine.h"
#include "AlertRetry.h"

namespace SQM
{
    namespace CloseSeq
    {
        constexpr size_t ALERT_QUEUE_CAPACITY = 6;

        struct QueuedAlert
        {
            Alerts::Alert alert;
            bool critical = false;   // Alerts::isCritical(alert.type)
            bool resolvable = false; // dropped when its condition ends (resolve())
            uint32_t serial = 0;     // which push this is (a new one per push, never 0)
            uint32_t id = 0;         // the dispatcher's record of the last send (0: not stored)
            bool sent = false;       // handed to the dispatcher since it was queued
            AlertRetry retry;
        };

        // How one send went, from the dispatcher's channels: one took it; one
        // is still trying; one failed or is blocked for now (no network, the
        // broker not connected, another HTTPS request): sent again later; none
        // switched on and able to: nothing to retry.
        Delivery deliveryFrom(bool anySent, bool anyPending, bool anyFailedOrBlockedForNow);

        class AlertQueue
        {
        public:
            using Send = std::function<uint32_t(const QueuedAlert &)>; // returns the record id (0: not stored)
            using Probe = std::function<Delivery(uint32_t)>;

            // A new alert: replaces a waiting one of the same event. Returns its serial.
            uint32_t push(const Alerts::Alert &alert, bool resolvable);
            // The condition behind the event's alert ended: a resolvable one waiting is dropped.
            void resolve(Alerts::AlertType type);
            // The longest gap between two sends (the reminder interval, at most 30 min).
            void setMaxGap(uint64_t ms);
            // Sends what is due now; drops what a channel took.
            void step(uint64_t nowMs, bool networkUp, const Send &send, const Probe &probe);

            const QueuedAlert *find(Alerts::AlertType type) const;
            size_t size() const { return entries.size(); }

        private:
            bool settle(QueuedAlert &entry, uint64_t nowMs, bool upAgain, const Probe &probe);
            void makeRoom();

            std::vector<QueuedAlert> entries; // oldest first
            uint32_t nextSerial = 1;
            uint64_t maxGapMs = ALERT_RETRY_MAX_MS;
            bool wasUp = false;
        };
    } // namespace CloseSeq
} // namespace SQM
