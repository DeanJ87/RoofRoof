#pragma once

// Home Assistant MQTT discovery (SQMeter spec 013 FR-008): the device's
// entities as retained config messages under the discovery prefix. RoofRoof spec 008
// adds the roof's cover, sensors and fault sensor (contracts/mqtt.md).

#include <ArduinoJson.h>
#include <cstddef>
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

        // Calls `publish(topic, config)` for every entity: its config document
        // when its group is enabled, null (an empty payload removes the entity)
        // when not. The document is reused for the next entity (spec 031).
        void forEach(
            const Device &device,
            const Groups &groups,
            const std::function<void(const std::string &topic, const JsonDocument *config)> &publish);
    } // namespace HaDiscovery
} // namespace SQM
