// The safety and park settings (spec 004 US4): defaults off, validation the
// web UI shares (contracts/settings.md), and configs from before spec 004
// loading with every new source off.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"

using namespace SQM;

namespace
{
    std::string rejectReason(const std::string &json)
    {
        const Config base = Config::createDefault();
        std::string error;
        const auto cfg = Config::fromJson(json, &base, &error);
        return cfg ? std::string() : error;
    }

    Config applied(const std::string &json)
    {
        const Config base = Config::createDefault();
        std::string error;
        const auto cfg = Config::fromJson(json, &base, &error);
        TEST_ASSERT_TRUE_MESSAGE(cfg.has_value(), error.c_str());
        return *cfg;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_defaults_are_off()
{
    const Config c = Config::createDefault();
    TEST_ASSERT_FALSE(c.safety.httpEnabled);
    TEST_ASSERT_TRUE(c.safety.httpUrl.empty());
    TEST_ASSERT_FALSE(c.safety.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("sqmeter", c.safety.mqttTopic.c_str());
    TEST_ASSERT_EQUAL_UINT32(30, c.safety.staleSeconds);
    TEST_ASSERT_FALSE(c.safety.rainContact);
    TEST_ASSERT_TRUE(c.park.ninaUrl.empty());
    TEST_ASSERT_TRUE(c.park.switches == ParkConfig::Switches::None);
}

void test_settings_json_shape()
{
    DynamicJsonDocument doc(4096);
    TEST_ASSERT_FALSE(deserializeJson(doc, Config::createDefault().toJson()));
    TEST_ASSERT_FALSE(doc["safety"]["http"]["enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("", doc["safety"]["http"]["url"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("sqmeter", doc["safety"]["mqtt"]["topic"].as<const char *>());
    TEST_ASSERT_EQUAL_UINT32(30, doc["safety"]["staleSeconds"].as<uint32_t>());
    TEST_ASSERT_FALSE(doc["safety"]["rainContact"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("", doc["park"]["nina"]["url"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("v2", doc["park"]["nina"]["api"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("none", doc["park"]["switches"].as<const char *>());
}

void test_full_settings_round_trip()
{
    const Config c = applied(R"({"safety":{"http":{"enabled":true,"url":"http://sqmeter.local/api/safety"},)"
                             R"("mqtt":{"enabled":true,"topic":"obs/sqmeter"},"staleSeconds":120,"rainContact":true},)"
                             R"("mqtt":{"enabled":true,"broker":"10.0.0.2","topic":"roofroof"},)"
                             R"("park":{"nina":{"url":"http://[fd00::20]:1888","api":"v2"},"switches":"two"}})");
    TEST_ASSERT_TRUE(c.safety.httpEnabled);
    TEST_ASSERT_EQUAL_STRING("http://sqmeter.local/api/safety", c.safety.httpUrl.c_str());
    TEST_ASSERT_TRUE(c.safety.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("obs/sqmeter", c.safety.mqttTopic.c_str());
    TEST_ASSERT_EQUAL_UINT32(120, c.safety.staleSeconds);
    TEST_ASSERT_TRUE(c.safety.rainContact);
    TEST_ASSERT_EQUAL_STRING("http://[fd00::20]:1888", c.park.ninaUrl.c_str());
    TEST_ASSERT_TRUE(c.park.switches == ParkConfig::Switches::Two);

    const auto again = Config::fromJson(c.toJson());
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL_STRING(c.toJson().c_str(), again->toJson().c_str());
}

void test_switch_choices()
{
    TEST_ASSERT_TRUE(applied(R"({"park":{"switches":"one"}})").park.switches == ParkConfig::Switches::One);
    TEST_ASSERT_TRUE(applied(R"({"park":{"switches":"none"}})").park.switches == ParkConfig::Switches::None);
}

void test_rejections_say_why()
{
    TEST_ASSERT_EQUAL_STRING(
        "SQMeter safety URL must start with http://",
        rejectReason(R"({"safety":{"http":{"enabled":true,"url":"https://sqm/api/safety"}}})").c_str());
    TEST_ASSERT_EQUAL_STRING("SQMeter safety URL must start with http://", rejectReason(R"({"safety":{"http":{"enabled":true}}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "SQMeter MQTT topic: use letters, numbers, _ and -, with / between levels",
        rejectReason(R"({"safety":{"mqtt":{"enabled":true,"topic":"sqm/#"}}})").c_str());
    TEST_ASSERT_EQUAL_STRING("N.I.N.A. URL must start with http://", rejectReason(R"({"park":{"nina":{"url":"nina:1888"}}})").c_str());
    TEST_ASSERT_EQUAL_STRING("N.I.N.A. API version must be v2", rejectReason(R"({"park":{"nina":{"api":"v3"}}})").c_str());
    TEST_ASSERT_EQUAL_STRING("Park switches must be none, one or two", rejectReason(R"({"park":{"switches":"both"}})").c_str());
    TEST_ASSERT_EQUAL_STRING("Park switches must be none, one or two", rejectReason(R"({"park":{"switches":2}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Settings: safety.rainContact must be true or false", rejectReason(R"({"safety":{"rainContact":"yes"}})").c_str());
}

void test_url_host_errors_are_named()
{
    const std::string reason = rejectReason(R"({"park":{"nina":{"url":"http://"}}})");
    TEST_ASSERT_EQUAL_STRING("N.I.N.A. URL: ", reason.substr(0, 14).c_str());
    const std::string http = rejectReason(R"({"safety":{"http":{"enabled":true,"url":"http://bad host/"}}})");
    TEST_ASSERT_EQUAL_STRING("SQMeter safety URL: ", http.substr(0, 20).c_str());
}

void test_disabled_sources_keep_any_url()
{
    // A switched-off source isn't checked, so turning it off never needs the URL fixed first.
    const Config c = applied(R"({"safety":{"http":{"enabled":false,"url":"https://old"}}})");
    TEST_ASSERT_EQUAL_STRING("https://old", c.safety.httpUrl.c_str());
}

void test_stale_time_boundaries()
{
    TEST_ASSERT_EQUAL_UINT32(10, applied(R"({"safety":{"staleSeconds":10}})").safety.staleSeconds);
    TEST_ASSERT_EQUAL_UINT32(600, applied(R"({"safety":{"staleSeconds":600}})").safety.staleSeconds);
    TEST_ASSERT_EQUAL_STRING(
        "Safety: stale time must be between 10 and 600 seconds", rejectReason(R"({"safety":{"staleSeconds":9}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Safety: stale time must be between 10 and 600 seconds", rejectReason(R"({"safety":{"staleSeconds":601}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "safety.staleSeconds must be a whole number in range", rejectReason(R"({"safety":{"staleSeconds":-30}})").c_str());
}

// A config stored before spec 004 has no safety or park section: it loads,
// and every new source stays off (constitution: a migration never turns a
// feature on).
void test_pre_004_config_loads_with_sources_off()
{
    Config stored = Config::createDefault();
    stored.mqtt.enabled = true;
    stored.mqtt.broker = "10.0.0.2";
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, stored.toJson(false, false));
    doc.remove("safety");
    doc.remove("park");
    std::string old;
    serializeJson(doc, old);

    Config loaded = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson(old, loaded, false));
    TEST_ASSERT_TRUE(loaded.mqtt.enabled);
    TEST_ASSERT_FALSE(loaded.safety.httpEnabled);
    TEST_ASSERT_FALSE(loaded.safety.mqttEnabled);
    TEST_ASSERT_FALSE(loaded.safety.rainContact);
    TEST_ASSERT_TRUE(loaded.park.ninaUrl.empty());
    TEST_ASSERT_TRUE(loaded.park.switches == ParkConfig::Switches::None);
}

void test_longest_settings_fit_the_stored_document()
{
    const std::string url = "http://" + std::string(120, 'a') + "/api/safety";
    Config c = Config::createDefault();
    c.safety.httpEnabled = true;
    c.safety.httpUrl = url;
    c.park.ninaUrl = "http://" + std::string(120, 'b') + ":1888";
    TEST_ASSERT_TRUE(c.toJson(false, false).size() < Config::MAX_PERSISTED_JSON_BYTES);
    const auto again = Config::fromJson(c.toJson());
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL_STRING(url.c_str(), again->safety.httpUrl.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_off);
    RUN_TEST(test_settings_json_shape);
    RUN_TEST(test_full_settings_round_trip);
    RUN_TEST(test_switch_choices);
    RUN_TEST(test_rejections_say_why);
    RUN_TEST(test_url_host_errors_are_named);
    RUN_TEST(test_disabled_sources_keep_any_url);
    RUN_TEST(test_stale_time_boundaries);
    RUN_TEST(test_pre_004_config_loads_with_sources_off);
    RUN_TEST(test_longest_settings_fit_the_stored_document);
    return UNITY_END();
}
