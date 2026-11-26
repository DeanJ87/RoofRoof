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
                const char *value; // sensors: the field of <base>/state the value comes from; spec 027 binary sensors: the whole template
                bool diagnostic;
                bool text = false; // spec 027: a text sensor (no state class)
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
                // Spec 022: on while a test-box bypass is in effect.
                {"binary_sensor", "test_box", "Test box bypass", Group::Roof, "mdi:test-tube", "problem", nullptr, nullptr, false},
                // Spec 027: what an observatory dashboard needs (docs/integration/home-assistant.md).
                {"binary_sensor",
                 "weather_safe",
                 "Weather safe",
                 Group::Roof,
                 "mdi:weather-night",
                 nullptr,
                 nullptr,
                 "{{ 'ON' if value_json.roof.weather == 'safe' else 'OFF' }}",
                 false},
                {"binary_sensor",
                 "mount_parked",
                 "Mount parked",
                 Group::Roof,
                 "mdi:telescope",
                 nullptr,
                 nullptr,
                 "{{ 'ON' if value_json.roof.parkConfirmed else 'OFF' }}",
                 false},
                {"sensor",
                 "close_phase",
                 "Automatic close",
                 Group::Roof,
                 "mdi:home-roof",
                 nullptr,
                 nullptr,
                 "value_json.roof.closePhase",
                 false,
                 true},
                {"binary_sensor",
                 "close_held",
                 "Automatic close held",
                 Group::Roof,
                 "mdi:hand-back-left",
                 "problem",
                 nullptr,
                 "{{ 'ON' if value_json.roof.closePhase in ['heldByPerson', 'halted'] else 'OFF' }}",
                 false},
                {"sensor",
                 "hold_lapses",
                 "Hold lapses in",
                 Group::Roof,
                 nullptr,
                 "duration",
                 "s",
                 "value_json.roof.closeRearmSeconds",
                 false},
                {"sensor",
                 "blocked",
                 "Roof blocked",
                 Group::Roof,
                 "mdi:alert-circle-outline",
                 nullptr,
                 nullptr,
                 "value_json.roof.blocked",
                 false,
                 true},
                {"sensor",
                 "park_wait",
                 "Waiting for park",
                 Group::Roof,
                 nullptr,
                 "duration",
                 "s",
                 "value_json.roof.parkWait.leftSeconds if value_json.roof.parkWait else 0",
                 false},
                {"sensor",
                 "daylight",
                 "Daylight",
                 Group::Roof,
                 "mdi:weather-sunny",
                 nullptr,
                 nullptr,
                 "value_json.roof.daylightState",
                 false,
                 true},
                {"sensor",
                 "sqmeter_reply",
                 "SQMeter last reply",
                 Group::Roof,
                 nullptr,
                 "duration",
                 "s",
                 "value_json.roof.sqmeter.lastReplySeconds",
                 true},
                {"binary_sensor",
                 "override",
                 "Checks overridden",
                 Group::Roof,
                 "mdi:shield-alert",
                 "problem",
                 nullptr,
                 "{{ 'ON' if value_json.roof.override else 'OFF' }}",
                 false},
                // Spec 030: a restart, a fault or the automatic close unable to move needs a person.
                {"binary_sensor",
                 "needs_person",
                 "Needs a person",
                 Group::Roof,
                 "mdi:account-alert",
                 "problem",
                 nullptr,
                 "{{ 'ON' if value_json.roof.needsPerson.active else 'OFF' }}",
                 false},
                {"binary_sensor",
                 "nina_reachable",
                 "N.I.N.A. reachable",
                 Group::Roof,
                 nullptr,
                 "connectivity",
                 nullptr,
                 "{{ 'ON' if value_json.roof.nina.reachable else 'OFF' }}",
                 true},
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
                if (!entity.text)
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

            // Spec 022: on while a bypass is in effect; which, until when and
            // the text as attributes.
            void addTestBox(JsonDocument &doc, const std::string &base)
            {
                doc["state_topic"] = base + "/state";
                doc["value_template"] = "{{ 'ON' if (value_json.roof.testBox or {}).active else 'OFF' }}";
                doc["json_attributes_topic"] = base + "/state";
                doc["json_attributes_template"] = "{{ (value_json.roof.testBox or {}) | tojson }}";
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
                else if (object == "test_box")
                    addTestBox(doc, base);
                else if (std::string(entity.component) == "binary_sensor")
                {
                    // Spec 027: the entity's own ON/OFF template.
                    doc["state_topic"] = base + "/state";
                    doc["value_template"] = entity.value;
                    if (entity.diagnostic)
                        doc["entity_category"] = "diagnostic";
                }
                else
                    addSensor(doc, entity, base);
            }

            void writeConfig(JsonDocument &doc, const Device &device, const Entity &entity, const Groups &groups)
            {
                const std::string &base = device.baseTopic;
                doc.clear();
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
            }
        } // namespace

        size_t count()
        {
            return sizeof(ENTITIES) / sizeof(ENTITIES[0]);
        }

        void publishOne(size_t index, const Device &device, const Groups &groups, JsonDocument &doc, const Publish &publish)
        {
            if (index >= count())
                return;
            const Entity &entity = ENTITIES[index];
            const std::string topic = device.prefix + "/" + entity.component + "/" + device.id + "/" + entity.object + "/config";
            const bool on = enabled(entity.group, groups);
            if (on)
                writeConfig(doc, device, entity, groups);
            publish(topic, on ? &doc : nullptr);
        }

        void forEach(const Device &device, const Groups &groups, const Publish &publish)
        {
            // One pool for every entity (spec 031: published straight from it).
            DynamicJsonDocument doc(CONFIG_DOC_BYTES);
            for (size_t i = 0; i < count(); ++i)
                publishOne(i, device, groups, doc, publish);
        }

        uint32_t markerOf(const std::string &key)
        {
            uint32_t hash = 2166136261u; // FNV-1a
            for (const char c : key)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 16777619u;
            }
            return hash == 0 ? 1 : hash; // 0 is "none"
        }

        void markerText(uint32_t marker, char (&out)[MARKER_TEXT_BYTES])
        {
            static const char DIGITS[] = "0123456789abcdef";
            for (size_t i = 0; i < 8; ++i)
                out[i] = DIGITS[(marker >> (28 - 4 * i)) & 0xF];
            out[8] = '\0';
        }

        uint32_t parseMarker(const char *text, size_t length)
        {
            if (length != 8)
                return 0;
            uint32_t value = 0;
            for (size_t i = 0; i < length; ++i)
            {
                const char c = text[i];
                const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1);
                if (digit < 0)
                    return 0;
                value = (value << 4) | static_cast<uint32_t>(digit);
            }
            return value;
        }

        void Pacer::start(uint32_t key, uint32_t nowMs, bool trustMarker)
        {
            active = true;
            this->key = key;
            startedMs = nowMs;
            lastMs = nowMs;
            index = 0;
            trust = trustMarker;
            seen = false;
            markerSent = false;
            wasSkipped = false;
        }

        void Pacer::markerSeen(uint32_t marker)
        {
            // Only before the first entity went out: once publishing, it finishes.
            if (active && index == 0 && marker == key)
                seen = true;
        }

        Pacer::Step Pacer::next(uint32_t nowMs, size_t entities) const
        {
            if (!active)
                return {Kind::Done, 0};
            if (index == 0 && !markerSent && trust && seen)
                return {Kind::Skip, 0};
            if (index == 0 && trust && nowMs - startedMs < MARKER_WAIT_MS)
                return {Kind::Wait, 0};
            const bool gapPassed = index == 0 || nowMs - lastMs >= GAP_MS;
            if (!gapPassed)
                return {Kind::Wait, 0};
            if (index < entities)
                return {Kind::Entity, index};
            return {Kind::Marker, 0};
        }

        void Pacer::done(const Step &step, uint32_t nowMs)
        {
            switch (step.kind)
            {
            case Kind::Entity:
                ++index;
                lastMs = nowMs;
                break;
            case Kind::Marker:
                markerSent = true;
                active = false;
                break;
            case Kind::Skip:
                wasSkipped = true;
                active = false;
                break;
            case Kind::Wait:
            case Kind::Done:
                break;
            }
        }
    } // namespace HaDiscovery
} // namespace SQM
