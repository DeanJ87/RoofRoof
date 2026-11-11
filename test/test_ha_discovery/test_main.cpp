// Home Assistant MQTT discovery (lib/HaDiscovery).

#include <ArduinoJson.h>
#include <map>
#include <string>
#include <unity.h>

#include "HaDiscovery.h"

using namespace SQM::HaDiscovery;

namespace
{
    const Device DEVICE{"roofroof_aabbccddeeff", "Observatory roof", "0.1.0", "roofroof", "homeassistant"};
    const char *const CONNECTIVITY_TOPIC = "homeassistant/binary_sensor/roofroof_aabbccddeeff/connectivity/config";
    const char *const SWITCH_TOPIC = "homeassistant/switch/roofroof_aabbccddeeff/alerts/config";

    std::map<std::string, std::string> announce(bool alertsSwitch, bool roofCommands = false, bool currentSense = false)
    {
        Groups groups;
        groups.alertsSwitch = alertsSwitch;
        groups.roofCommands = roofCommands;
        groups.currentSense = currentSense;
        std::map<std::string, std::string> out;
        forEach(DEVICE, groups, [&](const std::string &topic, const std::string &payload) { out[topic] = payload; });
        return out;
    }
    std::string topicOf(const char *component, const char *object)
    {
        return std::string("homeassistant/") + component + "/roofroof_aabbccddeeff/" + object + "/config";
    }

    DynamicJsonDocument parsed(const std::map<std::string, std::string> &out, const char *component, const char *object)
    {
        DynamicJsonDocument doc(2048);
        TEST_ASSERT_FALSE(deserializeJson(doc, out.at(topicOf(component, object))));
        return doc;
    }
} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_alerts_switch_config_names_its_topics(void)
{
    const std::string payload = announce(true).at(SWITCH_TOPIC);
    DynamicJsonDocument doc(1024);
    TEST_ASSERT_FALSE(deserializeJson(doc, payload));
    TEST_ASSERT_EQUAL_STRING("roofroof/alerts/armed", doc["state_topic"]);
    TEST_ASSERT_EQUAL_STRING("roofroof/alerts/armed/set", doc["command_topic"]);
    TEST_ASSERT_EQUAL_STRING("roofroof/availability", doc["availability"][0]["topic"]);
    TEST_ASSERT_EQUAL_STRING("roofroof_aabbccddeeff_alerts", doc["unique_id"]);
    TEST_ASSERT_EQUAL_STRING("RoofRoof", doc["device"]["manufacturer"]);
    TEST_ASSERT_EQUAL_STRING("roofroof_aabbccddeeff", doc["device"]["identifiers"][0]);
}

// SQMeter spec 020 D-13: the Alerts switch is only announced while alerts can
// go out, and removed (empty retained payload) otherwise.
void test_alerts_switch_removed_while_alerts_cannot_go_out(void)
{
    const auto out = announce(false);
    TEST_ASSERT_EQUAL_size_t(1, out.count(SWITCH_TOPIC));
    TEST_ASSERT_EQUAL_STRING("", out.at(SWITCH_TOPIC).c_str());
}

// Home Assistant shows the device even with alerts off: the connectivity
// entity follows the availability topic and its online/offline payloads.
void test_connectivity_is_announced_with_alerts_off(void)
{
    for (const bool alerts : {false, true})
    {
        const auto out = announce(alerts);
        DynamicJsonDocument doc(1024);
        TEST_ASSERT_FALSE(deserializeJson(doc, out.at(CONNECTIVITY_TOPIC)));
        TEST_ASSERT_EQUAL_STRING("connectivity", doc["device_class"]);
        TEST_ASSERT_EQUAL_STRING("roofroof/availability", doc["state_topic"]);
        TEST_ASSERT_EQUAL_STRING("online", doc["payload_on"]);
        TEST_ASSERT_EQUAL_STRING("offline", doc["payload_off"]);
        TEST_ASSERT_TRUE(doc["availability"].isNull());
        TEST_ASSERT_EQUAL_STRING("roofroof_aabbccddeeff_connectivity", doc["unique_id"]);
        TEST_ASSERT_EQUAL_STRING("roofroof_aabbccddeeff", doc["device"]["identifiers"][0]);
    }
}

// Spec 008 FR-010: the roof's cover, read-only until MQTT roof commands are on.
void test_roof_cover(void)
{
    for (const bool commands : {false, true})
    {
        const DynamicJsonDocument doc = parsed(announce(false, commands), "cover", "roof");
        TEST_ASSERT_EQUAL_STRING("Roof", doc["name"]);
        TEST_ASSERT_EQUAL_STRING("roofroof_aabbccddeeff_roof", doc["unique_id"]);
        TEST_ASSERT_EQUAL_STRING("roofroof/state", doc["state_topic"]);
        TEST_ASSERT_EQUAL_STRING(
            "{% set s = value_json.roof.state %}{{ s if s in ['open', 'opening', 'closed', 'closing', 'stopped'] else 'None' }}",
            doc["value_template"]);
        TEST_ASSERT_EQUAL_STRING("roofroof/availability", doc["availability"][0]["topic"]);
        TEST_ASSERT_EQUAL_STRING("roofroof_aabbccddeeff", doc["device"]["identifiers"][0]);
        TEST_ASSERT_TRUE(doc["position_topic"].isNull()); // no position: no set-position
        TEST_ASSERT_TRUE(doc["set_position_topic"].isNull());
        if (commands)
            TEST_ASSERT_EQUAL_STRING("roofroof/roof/set", doc["command_topic"]);
        else
            TEST_ASSERT_FALSE(doc.containsKey("command_topic"));
    }
}

void test_roof_sensors(void)
{
    const struct
    {
        const char *object;
        const char *name;
        const char *value;
        const char *unit;
        const char *deviceClass;
        bool diagnostic;
    } sensors[] = {
        {"position", "Roof position", "{{ value_json.roof.positionPercent }}", "%", nullptr, false},
        {"supply", "Supply voltage", "{{ value_json.roof.supplyVolts }}", "V", "voltage", false},
        {"motor_current", "Motor current", "{{ value_json.roof.motorAmps }}", "A", "current", false},
        {"hall", "Hall reading", "{{ value_json.roof.hall.counts }}", nullptr, nullptr, true},
    };
    const auto out = announce(false, false, true);
    for (const auto &s : sensors)
    {
        const DynamicJsonDocument doc = parsed(out, "sensor", s.object);
        TEST_ASSERT_EQUAL_STRING(s.name, doc["name"]);
        TEST_ASSERT_EQUAL_STRING((std::string("roofroof_aabbccddeeff_") + s.object).c_str(), doc["unique_id"]);
        TEST_ASSERT_EQUAL_STRING("roofroof/state", doc["state_topic"]);
        TEST_ASSERT_EQUAL_STRING(s.value, doc["value_template"]);
        TEST_ASSERT_EQUAL_STRING("measurement", doc["state_class"]);
        TEST_ASSERT_EQUAL_STRING("roofroof/availability", doc["availability"][0]["topic"]);
        if (s.unit != nullptr)
            TEST_ASSERT_EQUAL_STRING(s.unit, doc["unit_of_measurement"]);
        else
            TEST_ASSERT_FALSE(doc.containsKey("unit_of_measurement"));
        if (s.deviceClass != nullptr)
            TEST_ASSERT_EQUAL_STRING(s.deviceClass, doc["device_class"]);
        else
            TEST_ASSERT_FALSE(doc.containsKey("device_class"));
        TEST_ASSERT_EQUAL(s.diagnostic, doc["entity_category"] == "diagnostic");
        TEST_ASSERT_FALSE(doc.containsKey("command_topic"));
    }
}

// The motor current sensor only while current sense is on; removed otherwise.
void test_motor_current_only_with_current_sense(void)
{
    const auto off = announce(true, true, false);
    TEST_ASSERT_EQUAL_STRING("", off.at(topicOf("sensor", "motor_current")).c_str());
    TEST_ASSERT_FALSE(off.at(topicOf("sensor", "supply")).empty());
    const auto on = announce(true, true, true);
    TEST_ASSERT_FALSE(on.at(topicOf("sensor", "motor_current")).empty());
}

// On while a fault is latched, with the fault's details as attributes (constitution III).
void test_roof_fault_sensor(void)
{
    const DynamicJsonDocument doc = parsed(announce(false), "binary_sensor", "fault");
    TEST_ASSERT_EQUAL_STRING("Roof fault", doc["name"]);
    TEST_ASSERT_EQUAL_STRING("problem", doc["device_class"]);
    TEST_ASSERT_EQUAL_STRING("roofroof/state", doc["state_topic"]);
    TEST_ASSERT_EQUAL_STRING("{{ 'ON' if value_json.roof.fault else 'OFF' }}", doc["value_template"]);
    TEST_ASSERT_EQUAL_STRING("roofroof/state", doc["json_attributes_topic"]);
    TEST_ASSERT_EQUAL_STRING("{{ (value_json.roof.fault or {}) | tojson }}", doc["json_attributes_template"]);
    TEST_ASSERT_EQUAL_STRING("roofroof/availability", doc["availability"][0]["topic"]);
}

// Every entity, every time: announced or removed, never left out.
void test_every_entity_is_published_or_removed(void)
{
    for (const bool flag : {false, true})
    {
        const auto out = announce(flag, flag, flag);
        TEST_ASSERT_EQUAL_size_t(8, out.size());
        for (const auto &entry : out)
            if (!entry.second.empty())
                TEST_ASSERT_TRUE(entry.second.size() < 1024); // well inside the 3 KB MQTT buffer
    }
}

// The longest saved device name, base topic and prefix still give whole configs.
void test_long_names_fit(void)
{
    const Device longDevice{"roofroof_aabbccddeeff", std::string(64, 'n'), "0.1.0-beta.12", std::string(64, 't'), std::string(64, 'p')};
    Groups groups;
    groups.alertsSwitch = groups.roofCommands = groups.currentSense = true;
    int seen = 0;
    forEach(
        longDevice,
        groups,
        [&](const std::string &, const std::string &payload)
        {
            DynamicJsonDocument doc(4096);
            TEST_ASSERT_FALSE(deserializeJson(doc, payload));
            TEST_ASSERT_EQUAL_STRING(std::string(64, 'n').c_str(), doc["device"]["name"]);
            TEST_ASSERT_TRUE(payload.size() < 2048);
            ++seen;
        });
    TEST_ASSERT_EQUAL(8, seen);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_connectivity_is_announced_with_alerts_off);
    RUN_TEST(test_alerts_switch_config_names_its_topics);
    RUN_TEST(test_alerts_switch_removed_while_alerts_cannot_go_out);
    RUN_TEST(test_roof_cover);
    RUN_TEST(test_roof_sensors);
    RUN_TEST(test_motor_current_only_with_current_sense);
    RUN_TEST(test_roof_fault_sensor);
    RUN_TEST(test_every_entity_is_published_or_removed);
    RUN_TEST(test_long_names_fit);
    return UNITY_END();
}
