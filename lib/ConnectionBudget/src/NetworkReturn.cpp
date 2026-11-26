#include "NetworkReturn.h"

#include <cstdio>

namespace SQM
{
    namespace ConnectionBudget
    {
        bool MqttRetry::due(uint32_t nowMs, bool online, uint32_t sinceReturnMs) const
        {
            if (!online || sinceReturnMs < MQTT_RETURN_DELAY_MS)
                return false;
            return !tried || nowMs - lastEndMs >= intervalMs();
        }

        void MqttRetry::finished(uint32_t nowMs, bool connected)
        {
            tried = true;
            lastEndMs = nowMs;
            if (connected)
                failures = 0;
            else if (intervalMs() < MQTT_RETRY_MAX_MS)
                ++failures;
        }

        uint32_t MqttRetry::intervalMs() const
        {
            const uint32_t interval = MQTT_RETRY_MS << failures;
            return interval < MQTT_RETRY_MAX_MS ? interval : MQTT_RETRY_MAX_MS;
        }

        void NetworkReturn::update(bool online, uint32_t nowMs, uint32_t lastInboundMs)
        {
            if (!online)
            {
                if (up)
                    downSinceMs = nowMs;
                up = false;
                return;
            }
            if (!up)
            {
                up = true;
                linkUpMs = nowMs;
                begin(nowMs, everUp ? nowMs - downSinceMs : 0, everUp ? ReturnKind::LinkBack : ReturnKind::FirstUp);
                everUp = true;
                inboundSeenMs = lastInboundMs;
                return;
            }
            if (lastInboundMs == inboundSeenMs)
                return;
            // A client after a long silence, with the link up all along.
            const bool quiet = inboundSeenMs != 0 && lastInboundMs - inboundSeenMs >= QUIET_RETURN_MS &&
                               nowMs - linkUpMs >= QUIET_RETURN_MS && nowMs - returnAtMs >= QUIET_RETURN_MS;
            if (quiet)
                begin(nowMs, lastInboundMs - inboundSeenMs, ReturnKind::RequestsBack);
            inboundSeenMs = lastInboundMs;
        }

        void NetworkReturn::begin(uint32_t nowMs, uint32_t downMs, ReturnKind kind)
        {
            newest = count == 0 ? 0 : (newest + 1) % KEPT;
            if (count < KEPT)
                ++count;
            ReturnRecord &record = records[newest];
            record = ReturnRecord{};
            record.atMs = nowMs;
            record.downMs = downMs;
            record.kind = kind;
            returnAtMs = nowMs;
            refusedAtReturn = refusedSeen;
            if (kind != ReturnKind::FirstUp)
                ++returnCount;
        }

        void NetworkReturn::noteHeap(uint32_t nowMs, uint32_t largestBlock, uint32_t freeBytes)
        {
            if (!following(nowMs))
                return;
            ReturnRecord &record = records[newest];
            if (largestBlock < record.lowestLargest)
                record.lowestLargest = largestBlock;
            if (freeBytes < record.lowestFree)
                record.lowestFree = freeBytes;
        }

        void NetworkReturn::noteInbound(uint32_t nowMs, size_t live, uint32_t refusedTotal)
        {
            refusedSeen = refusedTotal;
            if (!following(nowMs))
                return;
            ReturnRecord &record = records[newest];
            record.refused = refusedTotal - refusedAtReturn;
            if (live > record.peakLive)
                record.peakLive = static_cast<uint16_t>(live < UINT16_MAX ? live : UINT16_MAX);
        }

        void NetworkReturn::noteMqttConnected(uint32_t nowMs)
        {
            if (count == 0 || !up)
                return;
            ReturnRecord &record = records[newest];
            if (record.mqttAfterMs == 0)
                record.mqttAfterMs = (nowMs - record.atMs) | 1; // 0 means "not yet"
        }

        void NetworkReturn::noteDiscovery(DiscoveryOutcome outcome)
        {
            if (count == 0)
                return;
            ReturnRecord &record = records[newest];
            if (record.discovery == DiscoveryOutcome::Pending)
                record.discovery = outcome;
        }

        const ReturnRecord &NetworkReturn::at(size_t newestFirst) const
        {
            return records[(newest + KEPT - newestFirst % KEPT) % KEPT];
        }

        namespace
        {
            const char *discoveryText(DiscoveryOutcome outcome)
            {
                switch (outcome)
                {
                case DiscoveryOutcome::Skipped:
                    return "discovery kept by the broker";
                case DiscoveryOutcome::Published:
                    return "discovery published again";
                case DiscoveryOutcome::Pending:
                    break;
                }
                return "discovery not sent yet";
            }

            // "link back after 121 s" and the like.
            int writeKind(const ReturnRecord &r, char *out, size_t size)
            {
                const unsigned downS = static_cast<unsigned>(r.downMs / 1000);
                switch (r.kind)
                {
                case ReturnKind::LinkBack:
                    return snprintf(out, size, "link back after %u s", downS);
                case ReturnKind::RequestsBack:
                    return snprintf(out, size, "requests back after %u s without any (link kept)", downS);
                case ReturnKind::FirstUp:
                    break;
                }
                return snprintf(out, size, "first on a network");
            }

            size_t clampWritten(int n, size_t size)
            {
                if (n < 0)
                    return 0;
                return static_cast<size_t>(n) < size ? static_cast<size_t>(n) : size - 1;
            }
        } // namespace

        size_t describe(const ReturnRecord &r, uint32_t nowMs, char *out, size_t size)
        {
            if (size == 0)
                return 0;
            size_t used = clampWritten(
                snprintf(
                    out, size, "  minute %5u, uptime %u s: ", static_cast<unsigned>(r.atMs / 60000), static_cast<unsigned>(r.atMs / 1000)),
                size);
            used += clampWritten(writeKind(r, out + used, size - used), size - used);
            const bool heapSeen = r.lowestLargest != UINT32_MAX;
            int n = heapSeen ? snprintf(
                                   out + used,
                                   size - used,
                                   "; lowest largest %u B, free %u B",
                                   static_cast<unsigned>(r.lowestLargest),
                                   static_cast<unsigned>(r.lowestFree))
                             : snprintf(out + used, size - used, "; no heap reading yet");
            used += clampWritten(n, size - used);
            n = snprintf(
                out + used,
                size - used,
                "; %u busy at connect, at most %u open",
                static_cast<unsigned>(r.refused),
                static_cast<unsigned>(r.peakLive));
            used += clampWritten(n, size - used);
            const unsigned tenths = static_cast<unsigned>(r.mqttAfterMs / 100);
            n = r.mqttAfterMs != 0 ? snprintf(out + used, size - used, "; MQTT after %u.%u s", tenths / 10, tenths % 10)
                                   : snprintf(out + used, size - used, "; MQTT not connected since");
            used += clampWritten(n, size - used);
            const bool still = nowMs - r.atMs < RETURN_FOLLOW_MS;
            n = snprintf(out + used, size - used, "; %s%s", discoveryText(r.discovery), still ? " (still following)" : "");
            used += clampWritten(n, size - used);
            return used;
        }

        bool returnedInMinute(const NetworkReturn &returns, uint32_t minuteIndex)
        {
            for (size_t i = 0; i < returns.size(); ++i)
                if (returns.at(i).atMs / 60000 == minuteIndex)
                    return true;
            return false;
        }
    } // namespace ConnectionBudget
} // namespace SQM
