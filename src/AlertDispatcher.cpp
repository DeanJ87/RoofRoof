#include "AlertDispatcher.h"
#include "BenchMode.h"
#include "DeviceCore.h"
#include "AlertDelivery.h"
#include "Logger.h"
#include "MQTTClient.h"
#include <Arduino.h>

#include "FixedRing.h"
#include <cstring>
#include <ctime>
#include <new>

// The records and the hand-over to the alert task (main loop side). The
// alert task's side, the HTTPS sends, is src/AlertSender.cpp (spec 053).

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Alerts";
        // Spec 045: the recent alerts, static data (one dispatcher), under its mutex.
        HeapSafe::FixedRing<AlertRecord, AlertDispatcher::MAX_RECORDS> recentRecords;

        int64_t epochNow()
        {
            const time_t now = time(nullptr);
            return now >= 1704067200 ? static_cast<int64_t>(now) : 0;
        }

        void wantedChannels(const AlertsConfig &cfg, uint8_t channelMask, bool (&wanted)[ALERT_CHANNEL_COUNT])
        {
            wanted[0] = cfg.mqttEnabled && (channelMask & alertChannelBit(AlertChannel::Mqtt));
            wanted[1] = cfg.pushoverEnabled && (channelMask & alertChannelBit(AlertChannel::Pushover));
            wanted[2] = cfg.ntfyEnabled && (channelMask & alertChannelBit(AlertChannel::Ntfy));
            wanted[3] = cfg.webhookEnabled && (channelMask & alertChannelBit(AlertChannel::Webhook));
        }

        // Switched on but inactive (e.g. MQTT alerts with MQTT off): skipped, never "failed".
        void skipBlocked(AlertRecord &record, const ChannelBlocks &blocked, bool (&wanted)[ALERT_CHANNEL_COUNT])
        {
            for (size_t i = 0; i < ALERT_CHANNEL_COUNT; ++i)
            {
                if (!wanted[i] || blocked[i] == nullptr)
                    continue;
                wanted[i] = false;
                record.status[i] = DeliveryStatus::Skipped;
                record.setDetail(i, blocked[i]);
                record.skippedForNow |= static_cast<uint8_t>(blocked.forNow & (1u << i));
            }
        }

        // The HTTP channels (everything but MQTT) still to send, marked
        // pending; returns their bits.
        uint8_t markPending(AlertRecord &record, const bool (&wanted)[ALERT_CHANNEL_COUNT])
        {
            uint8_t httpMask = 0;
            for (size_t i = 1; i < ALERT_CHANNEL_COUNT; ++i)
            {
                if (!wanted[i])
                    continue;
                record.status[i] = DeliveryStatus::Pending;
                httpMask |= static_cast<uint8_t>(1u << i);
            }
            return httpMask;
        }
    } // namespace

    const char *alertChannelName(AlertChannel channel)
    {
        switch (channel)
        {
        case AlertChannel::Mqtt:
            return "mqtt";
        case AlertChannel::Pushover:
            return "pushover";
        case AlertChannel::Ntfy:
            return "ntfy";
        case AlertChannel::Webhook:
            return "webhook";
        }
        return "unknown";
    }

    const char *deliveryStatusName(DeliveryStatus status)
    {
        switch (status)
        {
        case DeliveryStatus::NotSent:
            return "not_sent";
        case DeliveryStatus::Pending:
            return "pending";
        case DeliveryStatus::Sent:
            return "sent";
        case DeliveryStatus::Failed:
            return "failed";
        case DeliveryStatus::Skipped:
            return "skipped";
        }
        return "unknown";
    }

    AlertDispatcher::AlertDispatcher(MQTTClient *mqttClient, BusyCheck busy, BusyCheck moving)
        : mqtt(mqttClient),
          networkBusy(std::move(busy)),
          roofMoving(std::move(moving))
    {
    }

    AlertDispatcher::~AlertDispatcher()
    {
        // The dispatcher lives for the life of the firmware; the task is
        // never stopped, so the semaphores are intentionally not deleted.
    }

    void AlertDispatcher::begin()
    {
        mutex = xSemaphoreCreateMutex();
        wake = xSemaphoreCreateCounting(QUEUE_LENGTH, 0);
    }

    void AlertDispatcher::ensureTask()
    {
        bool expected = false;
        if (!taskRunning.compare_exchange_strong(expected, true))
            return;
        if (xTaskCreatePinnedToCore(taskEntry, "alerts", TASK_STACK_BYTES, this, TASK_PRIORITY, nullptr, TASK_CORE) != pdPASS)
        {
            taskRunning.store(false);
            Logger::error(TAG, "Couldn't start the alert task (low memory)");
        }
    }

    uint32_t AlertDispatcher::dispatch(
        const Alerts::Alert &given,
        const AlertsConfig &cfg,
        const std::string &deviceName,
        uint8_t channelMask,
        const ChannelBlocks &blocked)
    {
        // Spec 011: a bench board's alerts can't be mistaken for the roof's.
        const Alerts::Alert alert = Core::benchLabelled(given, Bench::BUILD);
        // Spec 045: built in static room (the main loop is the only caller),
        // not a 600 B record on the loop's stack.
        static AlertRecord record;
        std::memset(static_cast<void *>(&record), 0, sizeof(record)); // every field is set below or zero
        record.uptimeSeconds = millis() / 1000;
        record.epochSeconds = epochNow();
        record.type = alert.type;
        record.level = alert.level;
        HeapSafe::copyCut(record.title, sizeof(record.title), alert.title.c_str(), alert.title.size());
        HeapSafe::copyCut(record.message, sizeof(record.message), alert.message.c_str(), alert.message.size());

        bool wanted[ALERT_CHANNEL_COUNT];
        wantedChannels(cfg, channelMask, wanted);
        skipBlocked(record, blocked, wanted);

        // MQTT: publish right here on the main loop task.
        if (wanted[0])
        {
            const std::string payload = Delivery::alertJson(alert, deviceName, record.epochSeconds);
            const bool ok = mqtt != nullptr && mqtt->publishSubtopic("alerts", payload, false);
            record.status[0] = ok ? DeliveryStatus::Sent : DeliveryStatus::Failed;
            record.setDetail(0, ok ? "Published" : "MQTT not connected");
        }

        const uint8_t httpMask = markPending(record, wanted);

        const uint32_t id = store(record);
        Logger::info(TAG, "%s: %s", alert.title.c_str(), alert.message.c_str());
        if (httpMask != 0 && wake != nullptr && id != 0)
        {
            JobChannels channels{
                cfg.pushoverUserKey,
                cfg.pushoverAppToken,
                cfg.pushoverSound,
                cfg.ntfyServer,
                cfg.ntfyTopic,
                cfg.ntfyToken,
                cfg.webhookUrl,
                cfg.webhookAuthHeader,
                cfg.webhookInsecureTls};
            queueHttp(
                id, httpMask, new (std::nothrow) Job{id, alert, std::move(channels), deviceName, httpMask, Alerts::isCritical(alert.type)});
        }
        return id;
    }

    AlertDispatcher::Outcome AlertDispatcher::outcome(uint32_t id) const
    {
        if (id == 0 || !mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
            return Outcome::Failed;
        Outcome result = Outcome::Failed;
        for (size_t i = 0; i < recentRecords.size(); ++i)
        {
            const AlertRecord &record = recentRecords.at(i);
            if (record.id != id)
                continue;
            bool sent = false, pending = false, failed = false;
            for (DeliveryStatus status : record.status)
            {
                sent |= status == DeliveryStatus::Sent;
                pending |= status == DeliveryStatus::Pending;
                failed |= status == DeliveryStatus::Failed;
            }
            // Only skipped (switched on but blocked, e.g. MQTT alerts with MQTT
            // off) or not sent: no channel can take it, so resending won't help.
            // Spec 030: skipped for now (no network, the broker not connected) is
            // a failure to retry, not "no channel": a critical alert raised
            // before the network was up was dropped as undeliverable.
            failed |= record.skippedForNow != 0;
            result = sent ? Outcome::Delivered : pending ? Outcome::Pending : failed ? Outcome::Failed : Outcome::NoChannel;
            break;
        }
        xSemaphoreGive(mutex);
        return result;
    }

    uint32_t AlertDispatcher::store(AlertRecord &record)
    {
        if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
            return 0;
        record.id = nextId++;
        recentRecords.push(record); // spec 045: copied into the fixed ring, the oldest overwritten
        ++revision;
        xSemaphoreGive(mutex);
        return record.id;
    }

    // HTTPS channels go to the alert task. Spec 053: never waits (a few steps
    // over the queue's fixed slots under a spinlock); when full, the oldest
    // non-critical job is failed to make room ("Queue full"; the outbox, spec
    // 030, sends it again). Spec 035: a job that couldn't be allocated fails
    // the same way.
    void AlertDispatcher::queueHttp(uint32_t recordId, uint8_t channelMask, Job *job)
    {
        if (job == nullptr)
        {
            Logger::warn(TAG, "Low memory - alert not queued");
            for (size_t i = 1; i < ALERT_CHANNEL_COUNT; ++i)
                if (channelMask & (1u << i))
                    setStatus(recordId, static_cast<AlertChannel>(i), DeliveryStatus::Failed, "Low memory");
            return;
        }
        portENTER_CRITICAL(&jobsLock);
        const auto pushed = jobs.push(job, job->critical);
        portEXIT_CRITICAL(&jobsLock);
        if (pushed.accepted)
        {
            ++queuedCount;
            xSemaphoreGive(wake);
            ensureTask();
        }
        if (pushed.dropped)
            failJob(pushed.victim, "Queue full");
    }

    void AlertDispatcher::failJob(Job *job, const char *why)
    {
        Logger::warn(TAG, "Alert queue full - \"%s\" not sent by HTTPS", job->alert.title.c_str());
        for (size_t i = 1; i < ALERT_CHANNEL_COUNT; ++i)
            if (job->channelMask & (1u << i))
                setStatus(job->recordId, static_cast<AlertChannel>(i), DeliveryStatus::Failed, why);
        delete job;
    }

    AlertDeliveryStats AlertDispatcher::deliveryStats() const
    {
        AlertDeliveryStats stats;
        stats.queued = queuedCount.load();
        portENTER_CRITICAL(&jobsLock);
        stats.dropped = jobs.dropped();
        stats.memoryWaits = gate.memoryWaits();
        stats.moveWaits = gate.moveWaits();
        stats.gaveUp = gate.gaveUp();
        portEXIT_CRITICAL(&jobsLock);
        stats.stackLeast = stackLeast.load();
        stats.lastFreeBefore = lastFreeBefore.load();
        stats.lastLargestBefore = lastLargestBefore.load();
        stats.lastFreeDuring = lastFreeDuring.load();
        return stats;
    }

    void AlertDispatcher::setStatus(uint32_t recordId, AlertChannel channel, DeliveryStatus status, const char *detail)
    {
        if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
            return;
        if (AlertRecord *record = recentRecords.findNewest([recordId](const AlertRecord &r) { return r.id == recordId; }))
        {
            const size_t index = static_cast<size_t>(channel);
            record->status[index] = status;
            record->setDetail(index, detail);
            // The delivery task only skips for now (network, update, HTTPS busy).
            if (status == DeliveryStatus::Skipped)
                record->skippedForNow |= static_cast<uint8_t>(1u << index);
            ++revision;
        }
        xSemaphoreGive(mutex);
    }

    void AlertDispatcher::clearRecent()
    {
        if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            recentRecords.clear(); // the room stays (static, spec 045)
            ++revision;
            xSemaphoreGive(mutex);
        }
    }

    void AlertDispatcher::visitRecent(RecentVisit visit, void *context) const
    {
        if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
            return;
        for (size_t i = recentRecords.size(); i > 0; --i)
            visit(recentRecords.at(i - 1), context);
        xSemaphoreGive(mutex);
    }

    void AlertRecord::setDetail(size_t channel, const char *text)
    {
        if (channel < ALERT_CHANNEL_COUNT)
            HeapSafe::copyCut(detail[channel], sizeof(detail[channel]), text);
    }

} // namespace SQM
