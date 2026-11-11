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
                Roof,         // always (spec 008)
                CurrentSense, // only while current sense is fitted and on
            };

            struct Entity
            {
                const char *component;
                const char *object;
                const char *name;
                Group group;
                const char *icon;
                const char *deviceClass; // nullptr when the entity has none
                const char *unit;        // sensors: unit of measurement, or nullptr
                const char *value;       // sensors: the field of <base>/state the value comes from
                bool diagnostic;
            };

            // Spec 008 contracts/mqtt.md. The roof's values come from the retained
            // document on <base>/state (RoofIo::Mqtt::writeState); a null renders
            // "None", which Home Assistant shows as unknown.
            constexpr Entity ENTITIES[] = {
                {"binary_sensor", "connectivity", "Connectivity", Group::Connectivity, nullptr, "connectivity", nullptr, nullptr, false},
                {"switch", "alerts", "Alerts", Group::AlertsSwitch, "mdi:bell-ring", nullptr, nullptr, nullptr, false},
                {"cover", "roof", "Roof", Group::Roof, "mdi:home-roof", nullptr, nullptr, nullptr, false},
                {"sensor",
                 "position",
                 "Roof position",
                 Group::Roof,
                 "mdi:arrow-expand-horizontal",
                 nullptr,
                 "%",
                 "value_json.roof.positionPercent",
                 false},
                {"sensor", "supply", "Supply voltage", Group::Roof, nullptr, "voltage", "V", "value_json.roof.supplyVolts", false},
                {"sensor",
                 "motor_current",
                 "Motor current",
                 Group::CurrentSense,
                 nullptr,
                 "current",
                 "A",
                 "value_json.roof.motorAmps",
                 false},
                {"sensor", "hall", "Hall reading", Group::Roof, "mdi:magnet", nullptr, nullptr, "value_json.roof.hall.counts", true},
                {"binary_sensor", "fault", "Roof fault", Group::Roof, nullptr, "problem", nullptr, nullptr, false},
            };

            bool enabled(Group group, const Groups &groups)
            {
                switch (group)
                {
                case Group::Connectivity:
                case Group::Roof:
                    return true;
                case Group::AlertsSwitch:
                    return groups.alertsSwitch;
                case Group::CurrentSense:
                    return groups.currentSense;
                }
                return true;
            }

            // The cover (research R5): the roof's state passed through when Home
            // Assistant knows it, anything else (error, unknown) as unknown. Home
            // Assistant's default payloads (OPEN, CLOSE, STOP) and state names are
            // the device's. No command topic: read-only.
            void addCover(JsonDocument &doc, const std::string &base, const Groups &groups)
            {
                doc["state_topic"] = base + "/state";
                doc["value_template"] = "{% set s = value_json.roof.state %}"
                                        "{{ s if s in ['open', 'opening', 'closed', 'closing', 'stopped'] else 'None' }}";
                if (groups.roofCommands)
                    doc["command_topic"] = base + "/roof/set";
            }

            void addSensor(JsonDocument &doc, const Entity &entity, const std::string &base)
            {
                doc["state_topic"] = base + "/state";
                doc["value_template"] = std::string("{{ ") + entity.value + " }}";
                if (entity.unit != nullptr)
                    doc["unit_of_measurement"] = entity.unit;
                doc["state_class"] = "measurement";
                if (entity.diagnostic)
                    doc["entity_category"] = "diagnostic";
            }

            // On while a fault is latched; its kind, cause, measured value, limit
            // and text as attributes (constitution III).
            void addFault(JsonDocument &doc, const std::string &base)
            {
                doc["state_topic"] = base + "/state";
                doc["value_template"] = "{{ 'ON' if value_json.roof.fault else 'OFF' }}";
                doc["json_attributes_topic"] = base + "/state";
                doc["json_attributes_template"] = "{{ (value_json.roof.fault or {}) | tojson }}";
            }

            // Where the entity's state comes from, and how to command it.
            void addStateTopic(JsonDocument &doc, const Entity &entity, const std::string &base, const Groups &groups)
            {
                const std::string object = entity.object;
                if (entity.group == Group::Connectivity)
                {
                    // The availability topic itself is the state: MQTTClient
                    // publishes "online", and "offline" as the last will.
                    doc["state_topic"] = base + "/availability";
                    doc["payload_on"] = "online";
                    doc["payload_off"] = "offline";
                }
                else if (entity.group == Group::AlertsSwitch)
                {
                    doc["state_topic"] = base + "/alerts/armed";
                    doc["command_topic"] = base + "/alerts/armed/set";
                    doc["payload_on"] = "1";
                    doc["payload_off"] = "0";
                    doc["state_on"] = "1";
                    doc["state_off"] = "0";
                }
                else if (object == "roof")
                    addCover(doc, base, groups);
                else if (object == "fault")
                    addFault(doc, base);
                else
                    addSensor(doc, entity, base);
            }

            std::string payload(const Device &device, const Entity &entity, const Groups &groups)
            {
                const std::string &base = device.baseTopic;
                DynamicJsonDocument doc(2048); // the longest names: test_long_names_fit
                doc["name"] = entity.name;
                doc["unique_id"] = device.id + "_" + entity.object;
                doc["object_id"] = device.id + "_" + entity.object;
                // Connectivity has no availability of its own: offline is its state.
                if (entity.group != Group::Connectivity)
                    doc.createNestedArray("availability").createNestedObject()["topic"] = base + "/availability";
                addStateTopic(doc, entity, base, groups);
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
                publish(topic, enabled(entity.group, groups) ? payload(device, entity, groups) : std::string());
            }
        }
    } // namespace HaDiscovery
} // namespace SQM
