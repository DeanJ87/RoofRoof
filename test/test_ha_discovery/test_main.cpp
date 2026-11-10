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

    std::map<std::string, std::string> announce(bool alertsSwitch)
    {
        Groups groups;
        groups.alertsSwitch = alertsSwitch;
        std::map<std::string, std::string> out;
        forEach(DEVICE, groups, [&](const std::string &topic, const std::string &payload) { out[topic] = payload; });
        return out;
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

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_connectivity_is_announced_with_alerts_off);
    RUN_TEST(test_alerts_switch_config_names_its_topics);
    RUN_TEST(test_alerts_switch_removed_while_alerts_cannot_go_out);
    return UNITY_END();
}
