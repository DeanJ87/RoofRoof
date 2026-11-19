#include <unity.h>

#include <ArduinoJson.h>

#include <cmath>
#include <string>

#include "Config.h"

using namespace SQM;
using Source = LocationConfig::Source;

// Spec 020: the location's source (FR-001), the migration of settings saved
// before it (FR-006) and the device's ownership of the kept location (FR-008).

namespace
{
    // Unity's double assertions are off in this build.
    bool near(double expected, double actual)
    {
        return std::fabs(expected - actual) < 1e-9;
    }

    std::optional<Config> fromUi(const std::string &json, const Config &base, std::string *error = nullptr)
    {
        return Config::fromJson(json, &base, error);
    }

    // Settings as stored (not strict), over the defaults: what Config::load does.
    Config stored(const std::string &json)
    {
        Config cfg = Config::createDefault();
        std::string error;
        TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(json, cfg, false, &error), error.c_str());
        return cfg;
    }

    std::string locationJson(const Config &cfg)
    {
        DynamicJsonDocument doc(8192);
        TEST_ASSERT_FALSE(deserializeJson(doc, cfg.toJson()));
        std::string out;
        serializeJson(doc["location"], out);
        return out;
    }

    // A device following SQMeter with a kept GPS location.
    Config following()
    {
        Config cfg = Config::createDefault();
        cfg.location.source = Source::Sqmeter;
        cfg.location.set = true;
        cfg.location.latitude = 51.5074;
        cfg.location.longitude = -0.1278;
        cfg.location.gps = true;
        cfg.location.savedAt = 1763540000;
        return cfg;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_a_new_device_follows_sqmeter()
{
    const Config cfg = Config::createDefault();
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg.location.source);
    TEST_ASSERT_FALSE(cfg.location.set);
    TEST_ASSERT_EQUAL_STRING(
        "{\"source\":\"sqmeter\",\"set\":false,\"latitude\":0,\"longitude\":0,\"sqmeter\":{\"gps\":false,\"savedAt\":0}}",
        locationJson(cfg).c_str());
}

void test_the_kept_location_round_trips_through_storage()
{
    const Config cfg = following();
    const Config again = stored(cfg.toJson(false, false));
    TEST_ASSERT_EQUAL(Source::Sqmeter, again.location.source);
    TEST_ASSERT_TRUE(again.location.set);
    TEST_ASSERT_TRUE(near(51.5074, again.location.latitude));
    TEST_ASSERT_TRUE(near(-0.1278, again.location.longitude));
    TEST_ASSERT_TRUE(again.location.gps);
    TEST_ASSERT_EQUAL_UINT32(1763540000, again.location.savedAt);
}

// FR-006: a typed location saved before spec 020 stays typed, unchanged.
void test_a_stored_location_without_a_source_that_was_set_loads_manual()
{
    const Config cfg = stored("{\"location\":{\"set\":true,\"latitude\":51.48,\"longitude\":-0.01},\"daylight\":{\"mode\":\"close\"}}");
    TEST_ASSERT_EQUAL(Source::Manual, cfg.location.source);
    TEST_ASSERT_TRUE(cfg.location.set);
    TEST_ASSERT_TRUE(near(51.48, cfg.location.latitude));
    TEST_ASSERT_TRUE(near(-0.01, cfg.location.longitude));
    TEST_ASSERT_EQUAL(DaylightConfig::Mode::Close, cfg.daylight.mode);
}

void test_a_stored_location_without_a_source_not_set_loads_sqmeter()
{
    const Config cfg = stored("{\"location\":{\"set\":false,\"latitude\":0,\"longitude\":0}}");
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg.location.source);
    TEST_ASSERT_FALSE(cfg.location.set);
}

void test_stored_settings_without_a_location_load_sqmeter()
{
    const Config cfg = stored("{\"deviceName\":\"RoofRoof\"}");
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg.location.source);
    TEST_ASSERT_FALSE(cfg.location.set);
}

// The whole document a spec 019 device stored, less the new fields: only the
// source is new, everything else loads as it was.
void test_a_full_pre_020_document_keeps_its_typed_location()
{
    Config before = Config::createDefault();
    before.location.source = Source::Manual;
    before.location.set = true;
    before.location.latitude = -33.8688;
    before.location.longitude = 151.2093;
    DynamicJsonDocument doc(8192);
    TEST_ASSERT_FALSE(deserializeJson(doc, before.toJson(false, false)));
    doc["location"].remove("source");
    doc["location"].remove("sqmeter");
    std::string json;
    serializeJson(doc, json);
    const Config after = stored(json);
    TEST_ASSERT_EQUAL(Source::Manual, after.location.source);
    TEST_ASSERT_TRUE(near(-33.8688, after.location.latitude));
    TEST_ASSERT_TRUE(near(151.2093, after.location.longitude));
    TEST_ASSERT_EQUAL_STRING(before.toJson(false, false).c_str(), after.toJson(false, false).c_str());
}

void test_an_unknown_stored_source_falls_back_to_the_rule()
{
    const Config cfg = stored("{\"location\":{\"source\":\"satellite\",\"set\":true,\"latitude\":1,\"longitude\":2}}");
    TEST_ASSERT_EQUAL(Source::Manual, cfg.location.source);
}

void test_an_unknown_source_is_refused_from_the_ui()
{
    std::string error;
    TEST_ASSERT_FALSE(fromUi("{\"location\":{\"source\":\"satellite\"}}", Config::createDefault(), &error).has_value());
    TEST_ASSERT_EQUAL_STRING("Location: source must be sqmeter or manual", error.c_str());
    TEST_ASSERT_FALSE(fromUi("{\"location\":{\"source\":1}}", Config::createDefault(), &error).has_value());
}

// FR-008: a page opened before the last read sends an older location back.
void test_a_save_that_keeps_sqmeter_keeps_the_devices_location()
{
    const Config base = following();
    const auto cfg = fromUi(
        "{\"location\":{\"source\":\"sqmeter\",\"set\":true,\"latitude\":10,\"longitude\":20,\"sqmeter\":{\"gps\":false,\"savedAt\":5}}}",
        base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg->location.source);
    TEST_ASSERT_TRUE(near(51.5074, cfg->location.latitude));
    TEST_ASSERT_TRUE(near(-0.1278, cfg->location.longitude));
    TEST_ASSERT_TRUE(cfg->location.gps);
    TEST_ASSERT_EQUAL_UINT32(1763540000, cfg->location.savedAt);
}

void test_an_old_page_without_a_source_keeps_the_devices_location_when_not_set()
{
    const auto cfg = fromUi("{\"location\":{\"set\":false,\"latitude\":0,\"longitude\":0}}", following());
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg->location.source);
    TEST_ASSERT_TRUE(cfg->location.set);
    TEST_ASSERT_TRUE(near(51.5074, cfg->location.latitude));
}

void test_typing_a_location_without_a_source_makes_it_manual()
{
    const auto cfg = fromUi("{\"location\":{\"set\":true,\"latitude\":10,\"longitude\":20}}", following());
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(Source::Manual, cfg->location.source);
    TEST_ASSERT_TRUE(near(10, cfg->location.latitude));
}

void test_switching_to_manual_takes_the_sent_values()
{
    const auto cfg = fromUi("{\"location\":{\"source\":\"manual\",\"set\":true,\"latitude\":10,\"longitude\":20}}", following());
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(Source::Manual, cfg->location.source);
    TEST_ASSERT_TRUE(near(10, cfg->location.latitude));
    TEST_ASSERT_TRUE(near(20, cfg->location.longitude));
    // The device-owned fields are never read from the API.
    TEST_ASSERT_TRUE(cfg->location.gps);
}

void test_switching_to_sqmeter_keeps_the_typed_value_until_sqmeter_answers()
{
    Config base = Config::createDefault();
    base.location.source = Source::Manual;
    base.location.set = true;
    base.location.latitude = 10;
    base.location.longitude = 20;
    const auto cfg = fromUi("{\"location\":{\"source\":\"sqmeter\",\"set\":true,\"latitude\":10,\"longitude\":20}}", base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg->location.source);
    TEST_ASSERT_TRUE(cfg->location.set);
    TEST_ASSERT_TRUE(near(10, cfg->location.latitude));
    TEST_ASSERT_EQUAL_UINT32(0, cfg->location.savedAt);
}

void test_the_api_never_sets_the_device_owned_fields_on_a_manual_location()
{
    Config base = Config::createDefault();
    base.location.source = Source::Manual;
    const auto cfg = fromUi("{\"location\":{\"sqmeter\":{\"gps\":true,\"savedAt\":99}}}", base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_FALSE(cfg->location.gps);
    TEST_ASSERT_EQUAL_UINT32(0, cfg->location.savedAt);
}

void test_a_save_without_a_location_section_changes_nothing()
{
    const auto cfg = fromUi("{\"deviceName\":\"Roof\"}", following());
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(Source::Sqmeter, cfg->location.source);
    TEST_ASSERT_TRUE(near(51.5074, cfg->location.latitude));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_new_device_follows_sqmeter);
    RUN_TEST(test_the_kept_location_round_trips_through_storage);
    RUN_TEST(test_a_stored_location_without_a_source_that_was_set_loads_manual);
    RUN_TEST(test_a_stored_location_without_a_source_not_set_loads_sqmeter);
    RUN_TEST(test_stored_settings_without_a_location_load_sqmeter);
    RUN_TEST(test_a_full_pre_020_document_keeps_its_typed_location);
    RUN_TEST(test_an_unknown_stored_source_falls_back_to_the_rule);
    RUN_TEST(test_an_unknown_source_is_refused_from_the_ui);
    RUN_TEST(test_a_save_that_keeps_sqmeter_keeps_the_devices_location);
    RUN_TEST(test_an_old_page_without_a_source_keeps_the_devices_location_when_not_set);
    RUN_TEST(test_typing_a_location_without_a_source_makes_it_manual);
    RUN_TEST(test_switching_to_manual_takes_the_sent_values);
    RUN_TEST(test_switching_to_sqmeter_keeps_the_typed_value_until_sqmeter_answers);
    RUN_TEST(test_the_api_never_sets_the_device_owned_fields_on_a_manual_location);
    RUN_TEST(test_a_save_without_a_location_section_changes_nothing);
    return UNITY_END();
}
