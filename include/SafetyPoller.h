#pragma once

#include "NinaApi.h"
#include "SafetyInput.h"
#include "SafetySettings.h"
#include "SqmeterSite.h"
#include "SqmeterSync.h"
#include <ArduinoJson.h>
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
            // Spec 020: SQMeter's location (GET /api/status), and the site
            // generation it was read under.
            Site::LocationRead location;
            Site::ConfigRead settings; // SQMeter's settings, for the groups that follow it
            uint32_t siteGeneration = 0;
        };

        // Milliseconds since start, 64-bit (never wraps). The time base of
        // every safety record.
        static uint64_t nowMs();

        SafetyPoller();
        ~SafetyPoller();
        SafetyPoller(const SafetyPoller &) = delete;
        SafetyPoller &operator=(const SafetyPoller &) = delete;

        void begin(const Targets &targets);
        // New settings. Only the parts `change` names start again from nothing
        // (Safety::settingsChange decides); an unrelated save keeps every reading.
        void configure(const Targets &targets, const Safety::SettingsChange &change);
        // Ask N.I.N.A. to park the mount (spec 005 calls this). Any task; the
        // poller sends it before its next mount-info poll.
        void requestPark();
        Snapshot snapshot() const;

        // Spec 020: where SQMeter is (the weather source's address) and
        // whether the location follows it. A change reads again at once.
        void setSite(const std::string &address, bool followLocation, bool followSettings);
        // Set by the main loop every pass: the roof moves, a close waits or
        // the close sequence works. No location read starts meanwhile.
        void setRoofBusy(bool busy) { roofBusy.store(busy); }
        // The latest location read and its site generation, under the lock.
        Site::LocationRead locationRead(uint32_t &generation) const;
        // The latest read of SQMeter's settings (spec 020's sync), under the lock.
        Site::ConfigRead settingsRead() const;

        // One GET of a JSON document from `url`, parsed through `filter`
        // into `doc` as it arrives (no body buffer). Connecting and the
        // headers wait at most `connectMs` each; the body is read until
        // `totalMs` after the start. Any task but the async TCP one.
        static Site::Fetch fetchJson(
            const std::string &url, const JsonDocument &filter, JsonDocument &doc, uint32_t connectMs, uint32_t totalMs);

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
        void pollLocation(const std::string &url, uint32_t generation);
        // Spec 020: a location read in this pass, if due (never in a pass that
        // polled SQMeter's safety, never while the roof is busy).
        // At most one SQMeter site read per pass: the location, else the settings.
        void maybeReadSite(bool safetyPolled, bool safetyOn);
        void pollSettings(const std::string &url, uint32_t generation);
        Targets currentTargets(uint32_t &sqmeterGeneration, uint32_t &ninaGeneration) const;

        mutable SemaphoreHandle_t mutex = nullptr;
        TaskHandle_t task = nullptr;
        Targets targets;
        Snapshot state;
        std::atomic<bool> parkRequested{false};
        std::atomic<bool> roofBusy{false};
        // Spec 020: the site, under the lock; the schedule is the task's own.
        std::string siteAddress;
        bool siteFollows = false;
        bool settingsFollow = false;
        Site::LocationSchedule locationSchedule;
        Site::LocationSchedule settingsSchedule; // the same schedule rules, its own times
        // Connect and header wait, and the whole read (research R7): never
        // longer than one safety poll, and never in the same pass.
        static constexpr uint32_t LOCATION_CONNECT_MS = 1500;
        static constexpr uint32_t LOCATION_TOTAL_MS = Site::LocationSchedule::READ_MS;
        std::string siteStatusUrl;   // SQMeter's /api/status while the location follows it, else empty
        std::string siteSettingsUrl; // SQMeter's /api/config while any settings group follows it, else empty
        // The poll schedule: the poller task's own (a settings change makes
        // that part due at once).
        uint64_t nextSqmeterMs = 0;
        uint64_t nextNinaMs = 0;
    };
} // namespace SQM
