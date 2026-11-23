#pragma once

// Site data from the SQMeter at the same observatory (spec 020): its location
// from GET /api/status (sky) for the daylight failsafe, and the parts of its
// GET /api/config that RoofRoof can copy. Pure: the firmware
// (src/SafetyPoller.cpp, src/WebServerSite.cpp) and the demo only fetch.
// Nothing here is a safety input: SQMeter's location is only the site the
// sun's altitude is worked out for, as a typed-in one is.

#include <ArduinoJson.h>

#include <cstdint>
#include <optional>
#include <string>

#include "Config.h"

namespace SQM
{
    namespace Site
    {
        constexpr const char *STATUS_PATH = "/api/status";
        constexpr const char *CONFIG_PATH = "/api/config";

        // SQMeter's own `path` on the host of the weather source's address
        // (scheme, host and port of `safety.httpUrl`): "http://192.168.1.50/api/status".
        // Empty when the address isn't a valid http:// URL.
        std::string siteUrl(const std::string &address, const char *path);

        // One fetch: the HTTP status (0 = no reply, with `error`) and whether
        // the filtered body parsed.
        struct Fetch
        {
            int status = 0;
            bool parsed = false;
            std::string error;
            bool credentials = false; // SQMeter's login was sent
        };

        // Where to reach SQMeter: a full URL and its login (HTTP Basic, sent on
        // every request when the user name or password is set).
        struct Endpoint
        {
            std::string url;
            std::string username;
            std::string password;
            bool hasLogin() const { return !username.empty() || !password.empty(); }
        };

        // Spec 020: where the copy reads SQMeter's settings, and with which login.
        // A typed user name or password (the wizard, before saving) wins; an
        // empty or masked typed password means the saved one. The saved password
        // is only ever sent to the host and port it was saved for.
        struct TypedLogin
        {
            std::optional<std::string> username;
            std::optional<std::string> password;
        };
        Endpoint copyEndpoint(const SafetyConfig &saved, const std::string &address, const TypedLogin &typed);

        // Why there is no good location from SQMeter.
        enum class Problem : uint8_t
        {
            None,
            Waiting,         // not read yet
            NoAddress,       // SQMeter's address isn't set (or isn't http://)
            Unreachable,     // no reply
            Unreadable,      // not 200, or not SQMeter's status
            NoLocation,      // SQMeter has no location (locationSource "none", or no sky section)
            PasswordNeeded,  // 401/403 with no login sent
            PasswordRefused, // 401/403 with the login sent
        };
        // "waiting", "noAddress", "unreachable", "unreadable", "noLocation",
        // "passwordNeeded", "passwordRefused"; nullptr for None.
        const char *problemName(Problem problem);
        // Problem::PasswordNeeded or PasswordRefused for a 401 or 403, else Unreadable.
        Problem problemOfStatus(int status, bool credentialsSent);

        struct LocationRead
        {
            Problem problem = Problem::Waiting;
            double latitude = 0.0;
            double longitude = 0.0;
            bool gps = false;      // sky.locationSource "gps"
            uint64_t atMs = 0;     // when this read was taken (poller time)
            uint64_t goodAtMs = 0; // the last good read in this run; 0 = none
            std::string detail;    // what failed ("connection refused", "HTTP 404")
            bool good() const { return problem == Problem::None; }
        };

        // Document sizes for the filters and the filtered documents (room for
        // a 64-bit build; the ESP32 uses about half).
        constexpr size_t STATUS_FILTER_BYTES = 256;
        constexpr size_t STATUS_DOC_BYTES = 512;
        constexpr size_t CONFIG_FILTER_BYTES = 1536;
        constexpr size_t CONFIG_DOC_BYTES = 3072;

        // Only sky.latitude, sky.longitude and sky.locationSource of SQMeter's status.
        void statusFilter(JsonDocument &filter);
        // A read from one fetch of the status, filtered by statusFilter.
        LocationRead readLocation(const Fetch &fetch, const JsonDocument &doc, uint64_t atMs);
        // Keeps the time of the last good read across failed ones.
        void record(LocationRead &kept, const LocationRead &fresh);

        // When a location read is due. It never runs while the roof is busy
        // (moving, a close waiting, the automatic close working) or in a
        // poller pass that polled SQMeter's safety, so the poller's longest
        // pass is no longer than before (research R7).
        class LocationSchedule
        {
        public:
            static constexpr uint32_t FIRST_MS = 15000;   // after the poller starts
            static constexpr uint32_t GOOD_MS = 600000;   // after a good read
            static constexpr uint32_t FAILED_MS = 120000; // after a failed one
            // A read's longest time: none starts when the safety poll is due
            // within it, so it never delays that poll (SafetyPoller::LOCATION_TOTAL_MS).
            static constexpr uint32_t READ_MS = 3000;

            struct Pass
            {
                uint64_t nowMs = 0;
                bool enabled = false;      // the source is SQMeter and its address is set
                bool busy = false;         // the roof moves, a close waits or the close sequence works
                bool safetyPolled = false; // SQMeter's safety was polled in this pass
                uint64_t safetyNextMs = 0; // when SQMeter's safety poll is next due; 0 = that source is off
                uint32_t generation = 0;   // bumped by a change of the address or the source
            };

            bool due(const Pass &pass);
            void done(bool good, uint64_t nowMs);

        private:
            bool started = false;
            uint32_t seenGeneration = 0;
            uint64_t nextMs = 0;
        };

        // Changes smaller than this are never saved (about 110 m): a GPS fix
        // that jitters in its last digit never writes the flash.
        constexpr double SAVE_STEP_DEG = 0.001;
        // The location to save after a read, if any: only while the source is
        // SQMeter and the read is good, and then only when nothing is set,
        // the GPS flag differs, or a coordinate moved by SAVE_STEP_DEG or more.
        // `savedAt` is `epochSeconds` when the clock is set, else 0.
        std::optional<LocationConfig> locationToSave(
            const LocationConfig &current, const LocationRead &read, int64_t epochSeconds, bool clockSet);

        // FR-008 at the moment of saving: while the live settings keep a location
        // from SQMeter and the new ones still follow it, the new ones take the
        // live location (a settings copy taken before the last location save,
        // such as a staged restore, can't put an older one back).
        void keepDeviceLocation(const LocationConfig &live, LocationConfig &next);

        // The status document's "location" section (FR-010).
        void writeLocationStatus(JsonObject out, const LocationConfig &config, const LocationRead &read, bool addressSet, uint64_t nowMs);

        // What the copy can use of SQMeter's settings (FR-014). A secret is
        // only ever "set" or not: its text is never kept.
        struct SiteConfig
        {
            std::string name;
            bool hasLocation = false;
            bool locationSet = false;
            double latitude = 0.0;
            double longitude = 0.0;
            bool hasTime = false;
            std::string timezone;
            std::string server1;
            std::string server2;
            bool hasMqtt = false;
            std::string broker;
            uint16_t port = 1883;
            std::string username;
            std::string topic;
            bool passwordSet = false;
            bool hasNtfy = false;
            std::string ntfyServer;
            std::string ntfyTopic;
            bool ntfyTokenSet = false;
            bool hasPushover = false;
            bool pushoverUserKeySet = false;
            bool pushoverAppTokenSet = false;
        };

        // Only the parts of SQMeter's settings that SiteConfig holds.
        void configFilter(JsonDocument &filter);
        // False (with why) when the document isn't SQMeter's settings. Each
        // part an older SQMeter lacks is left out; v0.1's top-level
        // "timezone" is the time zone.
        bool readSiteConfig(const JsonDocument &doc, SiteConfig &out, std::string &why);
        // The site document of POST /api/sqmeter/site. Absent parts are null.
        void writeSiteDocument(JsonObject out, const std::string &address, const SiteConfig &site);
        // "http://192.168.1.50": the scheme, host and port shown as the address.
        std::string siteOrigin(const std::string &address);
    } // namespace Site
} // namespace SQM
