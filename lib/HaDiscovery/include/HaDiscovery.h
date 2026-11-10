#pragma once

// Home Assistant MQTT discovery (SQMeter spec 013 FR-008): the device's
// entities as retained config messages under the discovery prefix. Spec 008
// adds the roof's cover and sensors to the entity table.

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
        };

        // Calls `publish(topic, payload)` for every entity: its config when its
        // group is enabled, an empty payload (removes the entity) when not.
        void forEach(
            const Device &device,
            const Groups &groups,
            const std::function<void(const std::string &topic, const std::string &payload)> &publish);
    } // namespace HaDiscovery
} // namespace SQM
