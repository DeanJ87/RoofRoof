#pragma once

// Home Assistant MQTT discovery (SQMeter spec 013 FR-008): the device's
// entities as retained config messages under the discovery prefix. RoofRoof spec 008
// adds the roof's cover, sensors and fault sensor (contracts/mqtt.md).

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace SQM
{
    namespace HaDiscovery
    {
        // The device every entity belongs to.
        struct Device
        {
            std::string id;        // "roofroof_<mac hex>"
            std::string name;      // device name
            std::string version;   // firmware version
            std::string baseTopic; // MQTT base topic
            std::string prefix;    // discovery prefix, usually "homeassistant"
        };

        // Which entities are announced now.
        struct Groups
        {
            // The Alerts switch, only while alerts can go out (SQMeter spec 020 D-13).
            bool alertsSwitch = false;
            // The roof cover takes commands only while MQTT roof commands are on
            // (spec 008 FR-010); otherwise it is read-only.
            bool roofCommands = false;
            // The motor current sensor, only while current sense is on.
            bool currentSense = false;
        };

        // One entity's config document pool: the longest names (test_long_names_fit).
        constexpr size_t CONFIG_DOC_BYTES = 2048;

        using Publish = std::function<void(const std::string &topic, const JsonDocument *config)>;

        // How many entities there are (each has a config topic, announced or removed).
        size_t count();

        // Calls `publish(topic, config)` for entity `index`: its config document
        // (written into `doc`) when its group is enabled, null (an empty payload
        // removes the entity) when not.
        void publishOne(size_t index, const Device &device, const Groups &groups, JsonDocument &doc, const Publish &publish);

        // Every entity in turn, one document reused for all (spec 031).
        void forEach(const Device &device, const Groups &groups, const Publish &publish);

        // Spec 048: a retained marker on `<base>/discovery` says which set of
        // configs the broker holds (a hash of everything they depend on), so a
        // reconnect to a broker that kept them sends nothing again.
        constexpr const char *MARKER_SUBTOPIC = "discovery";
        constexpr size_t MARKER_TEXT_BYTES = 9;    // 8 hex digits
        uint32_t markerOf(const std::string &key); // never 0
        void markerText(uint32_t marker, char (&out)[MARKER_TEXT_BYTES]);
        uint32_t parseMarker(const char *text, size_t length); // 0 when not a marker

        // Spec 048: the configs one at a time after a (re)connect, GAP_MS
        // apart, instead of all in one pass of the main loop; the marker last,
        // once all went out. With `trustMarker` (this boot already published
        // this set), it first waits up to MARKER_WAIT_MS for the broker's
        // retained marker: a match skips the lot.
        class Pacer
        {
        public:
            static constexpr uint32_t GAP_MS = 200;
            static constexpr uint32_t MARKER_WAIT_MS = 2000;

            enum class Kind : uint8_t
            {
                Wait,   // nothing now
                Entity, // publish entity `index`
                Marker, // publish the marker (retained)
                Skip,   // the broker has them: nothing to send
                Done,   // finished (or never started)
            };
            struct Step
            {
                Kind kind;
                size_t index;
            };

            void start(uint32_t key, uint32_t nowMs, bool trustMarker);
            void markerSeen(uint32_t marker);
            Step next(uint32_t nowMs, size_t entities) const;
            // `step` (from next()) was carried out at `nowMs`.
            void done(const Step &step, uint32_t nowMs);
            bool running() const { return active; }
            bool skipped() const { return wasSkipped; }

        private:
            bool active = false;
            uint32_t key = 0;
            uint32_t startedMs = 0;
            uint32_t lastMs = 0;
            size_t index = 0;
            bool trust = false;
            bool seen = false;
            bool markerSent = false;
            bool wasSkipped = false;
        };
    } // namespace HaDiscovery
} // namespace SQM
