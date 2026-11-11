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
            uint64_t lastParkAtMs = 0; // 0 = none sent
            // Bumped when that part's settings change, so a reply to the old
            // settings is dropped.
            uint32_t sqmeterGeneration = 0;
            uint32_t ninaGeneration = 0;
        };

        // Milliseconds since start, 64-bit (never wraps). The time base of
        // every safety record.
        static uint64_t nowMs();

        SafetyPoller();
        ~SafetyPoller();
        SafetyPoller(const SafetyPoller &) = delete;
        SafetyPoller &operator=(const SafetyPoller &) = delete;

        void begin(const Targets &targets);
        // New settings. Only the part whose settings changed starts again from
        // nothing; an unrelated save keeps every reading. Any task.
        void configure(const Targets &targets);
        // Ask N.I.N.A. to park the mount (spec 005 calls this). Any task; the
        // poller sends it before its next mount-info poll.
        void requestPark();
        Snapshot snapshot() const;

    private:
        static constexpr uint32_t SQMETER_POLL_MS = 5000;
        static constexpr uint32_t NINA_POLL_MS = 2000;
        static constexpr uint32_t TICK_MS = 100;
        static constexpr uint32_t TASK_STACK_BYTES = 6144;

        static void taskEntry(void *arg);
        void run();
        void pollSqmeter(const Targets &targets, uint32_t generation);
        void pollNina(const Nina::Api &api, const Targets &targets, uint32_t generation);
        void sendPark(const Nina::Api &api, const Targets &targets, uint32_t generation);
        Targets currentTargets(uint32_t &sqmeterGeneration, uint32_t &ninaGeneration) const;

        mutable SemaphoreHandle_t mutex = nullptr;
        TaskHandle_t task = nullptr;
        Targets targets;
        Snapshot state;
        bool configured = false;
        std::atomic<bool> parkRequested{false};
        // The poll schedule: the poller task's own (a settings change makes
        // that part due at once).
        uint64_t nextSqmeterMs = 0;
        uint64_t nextNinaMs = 0;
    };
} // namespace SQM
