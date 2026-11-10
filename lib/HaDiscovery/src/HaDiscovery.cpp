#include "HaDiscovery.h"

#include <ArduinoJson.h>

namespace SQM
{
    namespace HaDiscovery
    {
        namespace
        {
            enum class Group : uint8_t
            {
                Connectivity, // always, whenever discovery is on
                AlertsSwitch,
            };

            struct Entity
            {
                const char *component;
                const char *object;
                const char *name;
                Group group;
                const char *icon;
                const char *deviceClass; // nullptr when the entity has none
            };

            constexpr Entity ENTITIES[] = {
                {"binary_sensor", "connectivity", "Connectivity", Group::Connectivity, nullptr, "connectivity"},
                {"switch", "alerts", "Alerts", Group::AlertsSwitch, "mdi:bell-ring", nullptr},
            };

            bool enabled(Group group, const Groups &groups)
            {
                switch (group)
                {
                case Group::Connectivity:
                    return true;
                case Group::AlertsSwitch:
                    return groups.alertsSwitch;
                }
                return true;
            }

            // Where the entity's state comes from, and how to command it.
            void addStateTopic(JsonDocument &doc, const Entity &entity, const std::string &base)
            {
                if (entity.group == Group::Connectivity)
                {
                    // The availability topic itself is the state: MQTTClient
                    // publishes "online", and "offline" as the last will.
                    doc["state_topic"] = base + "/availability";
                    doc["payload_on"] = "online";
                    doc["payload_off"] = "offline";
                }
                if (entity.group == Group::AlertsSwitch)
                {
                    doc["state_topic"] = base + "/alerts/armed";
                    doc["command_topic"] = base + "/alerts/armed/set";
                    doc["payload_on"] = "1";
                    doc["payload_off"] = "0";
                    doc["state_on"] = "1";
                    doc["state_off"] = "0";
                }
            }

            std::string payload(const Device &device, const Entity &entity)
            {
                const std::string &base = device.baseTopic;
                DynamicJsonDocument doc(1024);
                doc["name"] = entity.name;
                doc["unique_id"] = device.id + "_" + entity.object;
                doc["object_id"] = device.id + "_" + entity.object;
                // Connectivity has no availability of its own: offline is its state.
                if (entity.group != Group::Connectivity)
                    doc.createNestedArray("availability").createNestedObject()["topic"] = base + "/availability";
                addStateTopic(doc, entity, base);
                if (entity.icon != nullptr)
                    doc["icon"] = entity.icon;
                if (entity.deviceClass != nullptr)
                    doc["device_class"] = entity.deviceClass;

                JsonObject dev = doc.createNestedObject("device");
                dev.createNestedArray("identifiers").add(device.id);
                dev["name"] = device.name;
                dev["manufacturer"] = "RoofRoof";
                dev["model"] = "RoofRoof";
                dev["sw_version"] = device.version;

                std::string out;
                serializeJson(doc, out);
                return out;
            }
        } // namespace

        void forEach(
            const Device &device,
            const Groups &groups,
            const std::function<void(const std::string &topic, const std::string &payload)> &publish)
        {
            for (const Entity &entity : ENTITIES)
            {
                const std::string topic = device.prefix + "/" + entity.component + "/" + device.id + "/" + entity.object + "/config";
                publish(topic, enabled(entity.group, groups) ? payload(device, entity) : std::string());
            }
        }
    } // namespace HaDiscovery
} // namespace SQM
