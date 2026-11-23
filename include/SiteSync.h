#pragma once

#include "Config.h"
#include "SafetyPoller.h"
#include "SqmeterSync.h"
#include <ArduinoJson.h>
#include <functional>
#include <mutex>

namespace SQM
{
    // Spec 020: saves what the poller read from SQMeter (the location, and the
    // settings of the groups that follow it) into the live settings, on the
    // loop task, and writes the status's "location" and "sync" sections. The
    // decisions are lib/SafetyInputLogic's (SqmeterSite, SqmeterSync).
    class SiteSync
    {
    public:
        // `config` and `saveLock` are the live settings and the lock every
        // settings save holds; `applied(onlyLocation)` applies a saved change.
        SiteSync(Config &config, std::mutex &saveLock, SafetyPoller &poller, std::function<void(bool)> applied);

        // Once a second from the loop: never while `busy`, never waiting for a settings save.
        void handle(bool busy);
        // The status document's "location" and "sync" sections (any task).
        void writeStatus(JsonDocument &doc) const;

    private:
        bool saveLocation(const Site::LocationRead &read, time_t epoch);
        bool syncSettings(const Site::ConfigRead &read, time_t epoch);

        Config &config;
        std::mutex &saveLock;
        SafetyPoller &poller;
        std::function<void(bool)> applied;
        mutable std::mutex stateLock; // guards state (the last sync's refusals)
        Site::SyncState state;
        uint32_t lastCheckMs = 0;
        uint64_t locationAtMs = 0;
        uint64_t settingsAtMs = 0;
        uint8_t syncedFollows = 0;
    };
} // namespace SQM
