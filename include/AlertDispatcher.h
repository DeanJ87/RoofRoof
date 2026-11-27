#pragma once

#include "Config.h"
#include "AlertEngine.h"
#include "AlertSend.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace SQM
{
    class MQTTClient;

    enum class AlertChannel : uint8_t
    {
        Mqtt = 0,
        Pushover = 1,
        Ntfy = 2,
        Webhook = 3,
    };
    constexpr size_t ALERT_CHANNEL_COUNT = 4;
    constexpr uint8_t ALERT_CHANNELS_ALL = 0x0F;

    inline uint8_t alertChannelBit(AlertChannel channel)
    {
        return static_cast<uint8_t>(1u << static_cast<uint8_t>(channel));
    }
    const char *alertChannelName(AlertChannel channel);

    enum class DeliveryStatus : uint8_t
    {
        NotSent, // channel disabled / not requested
        Pending,
        Sent,
        Failed,
        Skipped, // e.g. no WiFi or an OTA update in progress
    };
    const char *deliveryStatusName(DeliveryStatus status);

    // Why each channel that's switched on can't be used right now (nullptr:
    // it can), e.g. "MQTT is off" - from the settings dependencies
    // (lib/SettingsDeps). Indexed by AlertChannel.
    // Spec 030: `forNow`, the channels blocked only until the network or the
    // broker is back (a resend may get through then).
    struct ChannelBlocks
    {
        std::array<const char *, ALERT_CHANNEL_COUNT> reasons{};
        uint8_t forNow = 0;
        const char *&operator[](size_t i) { return reasons[i]; }
        const char *operator[](size_t i) const { return reasons[i]; }
    };

    // Spec 045: fixed-size, so the recent alerts take no heap (they were
    // strings and template values: a dozen blocks per alert, kept until twenty
    // more alerts had come). Longer texts are cut, marked "...".
    struct AlertRecord
    {
        static constexpr size_t TITLE_BYTES = 64;
        static constexpr size_t MESSAGE_BYTES = 192;
        static constexpr size_t DETAIL_BYTES = 32;

        uint32_t id = 0;
        uint32_t uptimeSeconds = 0;
        int64_t epochSeconds = 0; // 0 if the clock wasn't set
        // Zero by default, so the ring of them is zero-filled static data (.bss).
        Alerts::AlertType type{};
        Alerts::AlertLevel level{};
        char title[TITLE_BYTES] = {};
        char message[MESSAGE_BYTES] = {};
        DeliveryStatus status[ALERT_CHANNEL_COUNT] = {};
        char detail[ALERT_CHANNEL_COUNT][DETAIL_BYTES] = {};
        // Spec 030: the channels skipped for now (no network, the broker not
        // connected, another HTTPS request, an update): sending again may work.
        uint8_t skippedForNow = 0;

        void setDetail(size_t channel, const char *text);
    };

    // Spec 053: how the HTTPS deliveries have gone since start (any task).
    struct AlertDeliveryStats
    {
        uint32_t queued = 0;            // HTTPS deliveries handed to the alert task
        uint32_t dropped = 0;           // pushed out of (or refused by) the full queue
        uint32_t memoryWaits = 0;       // checks that found the heap below the TLS floor
        uint32_t moveWaits = 0;         // checks that found the roof moving
        uint32_t gaveUp = 0;            // deliveries that waited SEND_GIVE_UP_MS and were skipped
        uint32_t stackLeast = 0;        // the least stack the alert task has had left (0: not run yet)
        uint32_t lastFreeBefore = 0;    // the last TLS send: 8-bit heap free when it started
        uint32_t lastLargestBefore = 0; // ... and its largest block
        uint32_t lastFreeDuring = 0;    // free heap with the session open (after the request)
    };

    // Delivers alerts. MQTT is published synchronously from the caller's
    // (main loop) task because PubSubClient isn't thread-safe; it is one
    // write on the loop's own connection. The HTTPS channels go through a
    // fixed queue (spec 053) to the alert task on core 0, away from the
    // roof's control task (core 1): the main loop only queues (no wait, no
    // allocation beyond the job), and the alert task waits for the roof to
    // stop and for the heap a TLS session needs before it starts one.
    class AlertDispatcher
    {
    public:
        using BusyCheck = std::function<bool()>;

        // `networkBusy`: an upload or update holds the network (HTTPS skipped
        // for now). `roofMoving` (spec 053, any task): HTTPS waits while true.
        AlertDispatcher(MQTTClient *mqtt, BusyCheck networkBusy, BusyCheck roofMoving);
        ~AlertDispatcher();

        AlertDispatcher(const AlertDispatcher &) = delete;
        AlertDispatcher &operator=(const AlertDispatcher &) = delete;

        void begin();

        // Main loop task only. `channelMask` restricts delivery (used for
        // per-channel test notifications); channels disabled in `cfg` are
        // never used, test or not. A channel switched on but blocked is
        // recorded as skipped with the reason and nothing is attempted.
        // Returns the stored record's id (0 if it couldn't be stored).
        uint32_t dispatch(
            const Alerts::Alert &alert,
            const AlertsConfig &cfg,
            const std::string &deviceName,
            uint8_t channelMask = ALERT_CHANNELS_ALL,
            const ChannelBlocks &blocked = ChannelBlocks{});

        // Spec 045: each recent alert, newest first, under the lock: no copy
        // of the list (it was copied onto the heap for every read). `visit`
        // must not call back into the dispatcher.
        using RecentVisit = void (*)(const AlertRecord &record, void *context);
        void visitRecent(RecentVisit visit, void *context) const;
        // How record `id` went over all its channels (spec 005 resends a
        // critical alert no channel took). Any task.
        enum class Outcome : uint8_t
        {
            Pending,   // a channel is still trying
            Delivered, // at least one channel sent it
            Failed,    // a channel tried and failed, or was skipped for now (none sent); or the record is gone
            NoChannel, // no channel was on and able to send (off, or blocked for good: MQTT off)
        };
        Outcome outcome(uint32_t id) const;
        void clearRecent();
        // Goes up whenever recent() would answer differently (a new alert, a
        // channel's result, a clear), so the UI can fetch the list only then.
        uint32_t recentRevision() const { return revision; }
        // Nothing queued and no delivery running (SQMeter's battery sleep waited
        // for this; RoofRoof has no battery or sleep). Any task.
        bool idle() const;
        // Spec 053: the HTTPS deliveries' counters. Any task.
        AlertDeliveryStats deliveryStats() const;

        static constexpr size_t MAX_RECORDS = 20;
        static constexpr size_t QUEUE_LENGTH = 8;
        // ESP-IDF stack sizes are bytes. Spec 053: a TLS handshake's depth
        // (mbedTLS, the certificate checks) is the deepest the task goes; the
        // least left is logged when it exits and counted in deliveryStats().
        static constexpr uint32_t TASK_STACK_BYTES = 8192;
        // Spec 053: the lowest priority above idle, on core 0 (the network's
        // core): the roof's control task runs on core 1 at priority 5, so a
        // handshake's seconds of arithmetic never run beside it.
        static constexpr UBaseType_t TASK_PRIORITY = 1;
        static constexpr BaseType_t TASK_CORE = 0;
        // The HTTPS task only exists while there's something to send: its
        // 8 KB stack is too much heap to hold for alerts that come hours apart.
        static constexpr uint32_t TASK_IDLE_EXIT_MS = 30000;

    private:
        // Spec 035: what the HTTPS channels use, not every event's wording
        // (a queued job held a copy of all the alert settings).
        struct JobChannels
        {
            std::string pushoverUserKey;
            std::string pushoverAppToken;
            std::string pushoverSound;
            std::string ntfyServer;
            std::string ntfyTopic;
            std::string ntfyToken;
            std::string webhookUrl;
            std::string webhookAuthHeader;
            bool webhookInsecureTls;
        };

        struct Job
        {
            uint32_t recordId;
            Alerts::Alert alert;
            JobChannels cfg;
            std::string deviceName;
            uint8_t channelMask;
            bool critical; // Alerts::isCritical: keeps its place in a full queue
        };

        static void taskEntry(void *arg);
        void run();
        bool takeJob(Job *&job);
        void deliver(const Job &job);
        void deliverOne(const Job &job, AlertChannel channel);
        bool waitToSend(const Job &job, AlertChannel channel, bool tls);
        bool mayStart(bool tls) const;
        void sendLocked(const Job &job, AlertChannel channel, bool tls);
        bool sendMeasured(const Job &job, AlertChannel channel, bool tls, std::string &detail);
        void noteStack();
        uint32_t store(AlertRecord &record); // the record's new id, 0 if it couldn't be stored
        void queueHttp(uint32_t recordId, uint8_t channelMask, Job *job);
        void failJob(Job *job, const char *why);
        void ensureTask();
        void setStatus(uint32_t recordId, AlertChannel channel, DeliveryStatus status, const char *detail);

        // One channel's request (Pushover, ntfy or the webhook); `freeDuring`:
        // the free heap with its session open.
        static bool post(const Job &job, AlertChannel channel, std::string &detail, uint32_t &freeDuring);

        MQTTClient *mqtt;
        BusyCheck networkBusy;
        BusyCheck roofMoving;
        // Spec 053: the HTTPS jobs, under jobsLock (a few steps over 8 slots).
        Alerts::SendQueue<Job *, QUEUE_LENGTH> jobs;
        mutable portMUX_TYPE jobsLock = portMUX_INITIALIZER_UNLOCKED;
        SemaphoreHandle_t wake = nullptr; // given for each job queued
        Alerts::SendGate gate;            // the alert task's only
        std::atomic<uint32_t> queuedCount{0};
        std::atomic<uint32_t> stackLeast{0};
        std::atomic<uint32_t> lastFreeBefore{0};
        std::atomic<uint32_t> lastLargestBefore{0};
        std::atomic<uint32_t> lastFreeDuring{0};
        std::atomic<bool> taskRunning{false};
        std::atomic<bool> delivering{false};
        SemaphoreHandle_t mutex = nullptr;
        uint32_t nextId = 1;
        std::atomic<uint32_t> revision{0};
    };

} // namespace SQM
