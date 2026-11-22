#pragma once

// Keeping RoofRoof's shared settings in step with the SQMeter at the same
// site (spec 020): each group that follows SQMeter (Config::sqmeterSync) takes
// SQMeter's values from its filtered GET /api/config, re-read on the location's
// schedule. Pure: the firmware (src/SafetyPoller.cpp, src/main.cpp) and the
// demo only fetch and save. Nothing here touches motion, the weather verdict,
// the park, Alpaca, WiFi, the device or host name, RoofRoof's own MQTT topic,
// or any secret (SQMeter masks them): a synced server change clears the
// matching saved secret instead, and the person is asked for it.

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "Config.h"
#include "SqmeterSite.h"

namespace SQM
{
    namespace Site
    {
        enum class Group : uint8_t
        {
            Time,
            Mqtt,
            Alerts,
        };
        constexpr size_t GROUP_COUNT = 3;
        const char *groupName(Group group); // "time", "mqtt", "alerts"
        SqmeterSyncConfig::Group &groupOf(SqmeterSyncConfig &sync, Group group);
        const SqmeterSyncConfig::Group &groupOf(const SqmeterSyncConfig &sync, Group group);

        // One read of SQMeter's settings (FR-025): good with its site, or why not.
        struct ConfigRead
        {
            Problem problem = Problem::Waiting; // NoLocation is never used here
            SiteConfig site;
            uint64_t atMs = 0;
            uint64_t goodAtMs = 0; // the last good read in this run; 0 = none
            std::string detail;
            bool good() const { return problem == Problem::None; }
        };
        // From one fetch of GET /api/config, filtered by configFilter.
        ConfigRead readConfig(const Fetch &fetch, const JsonDocument &doc, uint64_t atMs);
        // Keeps the last good read's time (and its site) across failed ones.
        void record(ConfigRead &kept, const ConfigRead &fresh);

        // What one sync did to a group.
        struct GroupOutcome
        {
            bool changed = false;
            std::string refused; // why SQMeter's values weren't taken (old values kept); empty = taken or unchanged
        };

        struct SyncOutcome
        {
            Config next; // the settings to save when any() (else the live ones)
            GroupOutcome groups[GROUP_COUNT];
            bool marked = false; // a following group was confirmed for the first time (syncedAt set)
            bool any() const;    // anything to save
            // "time; mqtt" for the log.
            std::string changedNames() const;
        };

        // SQMeter's values for each group that follows, applied to a copy of
        // `live`. A group's values must pass Config::validateChange, or that
        // group keeps its old values (`refused`). The MQTT group is held while
        // MQTT roof commands are on (a different broker would change who may
        // move the roof). A changed MQTT broker, port or user name clears the
        // saved MQTT password; a changed ntfy server clears the saved token.
        // `syncedAt` is set to `epochSeconds` (1 without a clock) when a group
        // changes or is confirmed for the first time.
        SyncOutcome syncFromSite(const Config &live, const SiteConfig &site, int64_t epochSeconds, bool clockSet);

        // FR-008 and FR-028 at the moment of saving: each group (and the
        // location) that follows SQMeter in both the live and the new settings,
        // and that the device has synced, keeps the live values (a settings copy
        // taken before the last sync can't put older ones back). Includes
        // keepDeviceLocation.
        void keepFollowed(const Config &live, Config &next);

        // The secrets a following group needs the person to type
        // ("mqttPassword", "ntfyToken", "pushoverKeys"): SQMeter has one set and
        // RoofRoof's is empty.
        std::vector<const char *> secretsNeeded(const Config &config, const SiteConfig &site, Group group);

        // The status document's "sync" section (FR-030).
        struct SyncState
        {
            std::string refused[GROUP_COUNT]; // the last sync's refusal per group
        };
        struct SyncView
        {
            const Config &config;
            const ConfigRead &read;
            const SyncState &state;
            bool addressSet = false; // SQMeter's address is set
            uint64_t nowMs = 0;
        };
        void writeSyncStatus(JsonObject out, const SyncView &view);
    } // namespace Site
} // namespace SQM
