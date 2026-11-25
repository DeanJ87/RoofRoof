#include "AlertQueue.h"

#include <algorithm>
#include <utility>

namespace SQM
{
    namespace CloseSeq
    {
        Delivery deliveryFrom(bool anySent, bool anyPending, bool anyFailedOrBlockedForNow)
        {
            if (anySent)
                return Delivery::Delivered;
            if (anyPending)
                return Delivery::Pending;
            return anyFailedOrBlockedForNow ? Delivery::Failed : Delivery::NoChannel;
        }

        uint32_t AlertQueue::push(const Alerts::Alert &alert, bool resolvable)
        {
            auto same = std::find_if(entries.begin(), entries.end(), [&alert](const QueuedAlert &e) { return e.alert.type == alert.type; });
            if (same != entries.end())
                entries.erase(same); // superseded: the newer one says where things stand
            else if (entries.size() >= ALERT_QUEUE_CAPACITY)
                makeRoom();
            QueuedAlert entry;
            entry.alert = alert;
            entry.critical = Alerts::isCritical(alert.type);
            entry.resolvable = resolvable;
            entry.serial = nextSerial++;
            if (nextSerial == 0)
                nextSerial = 1;
            entry.retry.setMaxGap(maxGapMs);
            const uint32_t serial = entry.serial;
            entries.push_back(std::move(entry));
            return serial;
        }

        // The oldest non-critical alert goes; with none, the oldest of all.
        void AlertQueue::makeRoom()
        {
            auto drop = std::find_if(entries.begin(), entries.end(), [](const QueuedAlert &e) { return !e.critical; });
            entries.erase(drop != entries.end() ? drop : entries.begin());
        }

        void AlertQueue::resolve(Alerts::AlertType type)
        {
            entries.erase(
                std::remove_if(
                    entries.begin(), entries.end(), [type](const QueuedAlert &e) { return e.resolvable && e.alert.type == type; }),
                entries.end());
        }

        void AlertQueue::setMaxGap(uint64_t ms)
        {
            maxGapMs = ms;
            for (QueuedAlert &entry : entries)
                entry.retry.setMaxGap(ms);
        }

        const QueuedAlert *AlertQueue::find(Alerts::AlertType type) const
        {
            for (const QueuedAlert &entry : entries)
                if (entry.alert.type == type)
                    return &entry;
            return nullptr;
        }

        // A sent alert: true when it leaves the queue (a channel took it, or none
        // can). Back on the network, one that no channel took goes out at once.
        bool AlertQueue::settle(QueuedAlert &entry, uint64_t nowMs, bool upAgain, const Probe &probe)
        {
            const Delivery delivery = entry.id == 0 || !probe ? Delivery::Failed : probe(entry.id);
            if (delivery == Delivery::Delivered || delivery == Delivery::NoChannel)
                return true;
            if (upAgain && delivery != Delivery::Pending)
            {
                entry.sent = false;
                entry.retry.clear(); // the back-off starts again from its first gap
            }
            else if (entry.retry.resendDue(delivery, nowMs))
                entry.sent = false;
            return false;
        }

        QueuedAlert *AlertQueue::nextDue()
        {
            QueuedAlert *next = nullptr;
            for (QueuedAlert &entry : entries)
                if (!entry.sent && (next == nullptr || (entry.critical && !next->critical)))
                    next = &entry;
            return next;
        }

        void AlertQueue::step(uint64_t nowMs, bool networkUp, const Send &send, const Probe &probe)
        {
            const bool upAgain = networkUp && !wasUp;
            wasUp = networkUp;
            for (size_t i = 0; i < entries.size();)
            {
                QueuedAlert &entry = entries[i];
                if (entry.sent && settle(entry, nowMs, upAgain, probe))
                    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                else
                    ++i;
            }
            QueuedAlert *entry = networkUp && send ? nextDue() : nullptr;
            if (entry == nullptr)
                return;
            const bool again = entry->retry.active();
            entry->id = send(*entry);
            entry->sent = true;
            if (again)
                entry->retry.resent(nowMs);
            else
                entry->retry.sent(nowMs);
        }
    } // namespace CloseSeq
} // namespace SQM
