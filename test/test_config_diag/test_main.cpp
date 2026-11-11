#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"

using namespace SQM;

// Spec 017 FR-001: the three diagnostics settings, all off by default and for
// settings stored before they existed.

namespace
{
    std::string diagJson(const Config &cfg)
    {
        DynamicJsonDocument doc(8192);
        TEST_ASSERT_FALSE(deserializeJson(doc, cfg.toJson()));
        std::string out;
        serializeJson(doc["diag"], out);
        return out;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_defaults_are_all_off()
{
    const Config cfg = Config::createDefault();
    TEST_ASSERT_FALSE(cfg.diag.enabled);
    TEST_ASSERT_FALSE(cfg.diag.keepSummaries);
    TEST_ASSERT_FALSE(cfg.diag.mqtt);
    TEST_ASSERT_EQUAL_STRING("{\"enabled\":false,\"keepSummaries\":false,\"mqtt\":false}", diagJson(cfg).c_str());
}

void test_each_setting_round_trips()
{
    const Config base = Config::createDefault();
    const auto cfg = Config::fromJson("{\"diag\":{\"enabled\":true,\"keepSummaries\":true,\"mqtt\":true}}", &base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_TRUE(cfg->diag.enabled);
    TEST_ASSERT_TRUE(cfg->diag.keepSummaries);
    TEST_ASSERT_TRUE(cfg->diag.mqtt);
    const auto again = Config::fromJson(cfg->toJson());
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_TRUE(again->diag.enabled && again->diag.keepSummaries && again->diag.mqtt);
}

void test_stored_settings_without_diag_load_all_off()
{
    Config stored = Config::createDefault();
    std::string json = stored.toJson();
    const size_t at = json.find(",\"diag\":");
    TEST_ASSERT_TRUE(at != std::string::npos);
    const size_t end = json.find('}', at);
    json.erase(at, end - at + 1);
    TEST_ASSERT_TRUE(json.find("\"diag\"") == std::string::npos);
    const auto cfg = Config::fromJson(json);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_FALSE(cfg->diag.enabled || cfg->diag.keepSummaries || cfg->diag.mqtt);
}

void test_a_partial_save_keeps_diag()
{
    Config base = Config::createDefault();
    base.diag.enabled = true;
    base.diag.mqtt = true;
    const auto cfg = Config::fromJson("{\"deviceName\":\"Roof\"}", &base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_TRUE(cfg->diag.enabled);
    TEST_ASSERT_TRUE(cfg->diag.mqtt);
    TEST_ASSERT_FALSE(cfg->diag.keepSummaries);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_all_off);
    RUN_TEST(test_each_setting_round_trips);
    RUN_TEST(test_stored_settings_without_diag_load_all_off);
    RUN_TEST(test_a_partial_save_keeps_diag);
    return UNITY_END();
}
