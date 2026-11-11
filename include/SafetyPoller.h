#pragma once

#include "NinaApi.h"
#include "SafetyInput.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace SQM
{
    // Polls SQMeter's GET /api/safety and N.I.N.A.'s mount info over plain
    // http, and sends a park request when asked, on its own task (constitution
    // VI: outgoing HTTP never blocks the loop, the web server or the motion
    // task). It only fetches and records; lib/SafetyInputLogic and
    // lib/NinaLogic decide what the replies mean (spec 004).
    class SafetyPoller
    {
    public:
        // What to poll (from the settings).
        struct Targets
        {
            bool sqmeterEnabled = false;
            std::string sqmeterUrl; // full URL of /api/safety
            std::string ninaUrl;    // base URL; empty = not configured
            Nina::ApiVersion ninaApi = Nina::ApiVersion::V2;
        };

        // The latest of everything, copied out under the lock.
        struct Snapshot
        {
            Safety::SourceRecord sqmeter;
            Nina::MountReading nina;
            bool parkPending = false;  // asked for, not sent yet
            Nina::ParkReply lastPark;  // the latest park request's outcome
            uint32_t lastParkAtMs = 0; // 0 = none sent
            uint32_t generation = 0;   // bumped by configure(), so readers can tell old records
        };

        SafetyPoller();
        ~SafetyPoller();
        SafetyPoller(const SafetyPoller &) = delete;
        SafetyPoller &operator=(const SafetyPoller &) = delete;

        void begin(const Targets &targets);
        // New settings: records start again from nothing. Any task.
        void configure(const Targets &targets);
        // Ask N.I.N.A. to park the mount (spec 005 calls this). Any task; the
        // poller sends it before its next mount-info poll.
        void requestPark();
        Snapshot snapshot() const;

    private:
        static constexpr uint32_t SQMETER_POLL_MS = 5000;
        static constexpr uint32_t NINA_POLL_MS = 2000;
        static constexpr uint16_t HTTP_TIMEOUT_MS = 3000;
        static constexpr uint32_t TICK_MS = 100;
        static constexpr uint32_t TASK_STACK_BYTES = 6144;

        static void taskEntry(void *arg);
        void run();
        void pollSqmeter(const Targets &targets, uint32_t generation);
        void pollNina(const Nina::Api &api, const Targets &targets, uint32_t generation);
        void sendPark(const Nina::Api &api, const Targets &targets, uint32_t generation);
        Targets currentTargets(uint32_t &generation) const;

        mutable SemaphoreHandle_t mutex = nullptr;
        TaskHandle_t task = nullptr;
        Targets targets;
        Snapshot state;
        std::atomic<bool> parkRequested{false};
        // Reset by configure() on another task.
        std::atomic<uint32_t> nextSqmeterMs{0};
        std::atomic<uint32_t> nextNinaMs{0};
    };
} // namespace SQM
