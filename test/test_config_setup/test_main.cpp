#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"

using namespace SQM;

// Spec 015: the setup wizard's state and the page to resume at (FR-001, FR-002).

namespace
{
    std::optional<Config> fromUi(const std::string &json, const Config &base, std::string *error = nullptr)
    {
        return Config::fromJson(json, &base, error);
    }

    std::string setupJson(const Config &cfg)
    {
        DynamicJsonDocument doc(8192);
        TEST_ASSERT_FALSE(deserializeJson(doc, cfg.toJson()));
        std::string out;
        serializeJson(doc["setup"], out);
        return out;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_defaults_are_new_at_the_first_page()
{
    const Config cfg = Config::createDefault();
    TEST_ASSERT_EQUAL(SetupConfig::State::New, cfg.setup.state);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.setup.step);
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"new\",\"step\":0}", setupJson(cfg).c_str());
}

void test_each_state_round_trips()
{
    const Config base = Config::createDefault();
    for (const char *name : {"new", "offered", "done"})
    {
        const std::string json = std::string("{\"setup\":{\"state\":\"") + name + "\",\"step\":7}}";
        const auto cfg = fromUi(json, base);
        TEST_ASSERT_TRUE_MESSAGE(cfg.has_value(), name);
        TEST_ASSERT_EQUAL_STRING((std::string("{\"state\":\"") + name + "\",\"step\":7}").c_str(), setupJson(*cfg).c_str());
        const auto again = Config::fromJson(cfg->toJson());
        TEST_ASSERT_TRUE(again.has_value());
        TEST_ASSERT_EQUAL(cfg->setup.state, again->setup.state);
        TEST_ASSERT_EQUAL_UINT8(7, again->setup.step);
    }
}

void test_an_unknown_state_is_refused_from_the_ui()
{
    const Config base = Config::createDefault();
    std::string error;
    TEST_ASSERT_FALSE(fromUi("{\"setup\":{\"state\":\"finished\"}}", base, &error).has_value());
    TEST_ASSERT_EQUAL_STRING("Setup: state must be new, offered or done", error.c_str());
    TEST_ASSERT_FALSE(fromUi("{\"setup\":{\"state\":true}}", base, &error).has_value());
}

void test_a_step_out_of_range_is_refused_from_the_ui()
{
    const Config base = Config::createDefault();
    std::string error;
    TEST_ASSERT_TRUE(fromUi("{\"setup\":{\"step\":10}}", base).has_value());
    TEST_ASSERT_FALSE(fromUi("{\"setup\":{\"step\":11}}", base, &error).has_value());
    TEST_ASSERT_EQUAL_STRING("Setup: step must be between 0 and 10", error.c_str());
    TEST_ASSERT_FALSE(fromUi("{\"setup\":{\"step\":-1}}", base, &error).has_value());
    TEST_ASSERT_FALSE(fromUi("{\"setup\":{\"step\":300}}", base, &error).has_value());
}

// Stored settings never fail to load over the setup state: a bad value keeps the current one.
void test_a_bad_stored_value_keeps_the_current_one()
{
    Config cfg = Config::createDefault();
    cfg.setup.state = SetupConfig::State::Done;
    cfg.setup.step = 3;
    std::string reason;
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson("{\"setup\":{\"state\":\"finished\",\"step\":42}}", cfg, false, &reason), reason.c_str());
    TEST_ASSERT_EQUAL(SetupConfig::State::Done, cfg.setup.state);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.setup.step);
}

// A save from Settings leaves setup out: the device keeps it.
void test_a_save_without_setup_keeps_it()
{
    Config base = Config::createDefault();
    base.setup.state = SetupConfig::State::Offered;
    base.setup.step = 4;
    const auto cfg = fromUi("{\"deviceName\":\"Roof\"}", base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(SetupConfig::State::Offered, cfg->setup.state);
    TEST_ASSERT_EQUAL_UINT8(4, cfg->setup.step);
}

// A setup-only save (the banner's Dismiss, Skip setup, a step) changes
// nothing but the setup state: no motion setting, no command switch.
void test_a_setup_only_save_changes_nothing_else()
{
    Config base = Config::createDefault();
    base.roof.travelMm = 2100;
    base.roof.speedMmPerSecond = 41.5f;
    base.roof.topDutyPercent = 40;
    base.mqtt.roofCommands = true;
    base.daylight.mode = DaylightConfig::Mode::Close;
    base.alerts.ntfyEnabled = true;
    base.alerts.ntfyTopic = "roof";
    base.safety.httpEnabled = true;
    base.safety.httpUrl = "http://sqmeter.local/api/safety";
    const auto cfg = fromUi("{\"setup\":{\"state\":\"done\",\"step\":0}}", base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL(SetupConfig::State::Done, cfg->setup.state);
    Config expected = base;
    expected.setup = cfg->setup;
    TEST_ASSERT_EQUAL_STRING(expected.toJson(false, true).c_str(), cfg->toJson(false, true).c_str());
}

void test_stored_before_setup_is_a_document_without_the_section()
{
    TEST_ASSERT_TRUE(Config::storedBeforeSetup("{\"deviceName\":\"old\",\"wifi\":{\"ssid\":\"HomeNet\"}}"));
    TEST_ASSERT_TRUE(Config::storedBeforeSetup("{\"setup\":\"done\"}")); // not an object: never ours
    TEST_ASSERT_FALSE(Config::storedBeforeSetup("{\"setup\":{\"state\":\"new\",\"step\":0}}"));
    TEST_ASSERT_FALSE(Config::storedBeforeSetup(Config::createDefault().toJson(false, false)));
    TEST_ASSERT_FALSE(Config::storedBeforeSetup("not json"));
}

void test_marking_stored_before_setup_offers_the_wizard()
{
    Config cfg = Config::createDefault();
    cfg.setup.step = 5;
    cfg.markStoredBeforeSetup();
    TEST_ASSERT_EQUAL(SetupConfig::State::Offered, cfg.setup.state);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.setup.step);
}

// The setup state is in the stored document, so it survives a restart.
void test_the_stored_document_carries_setup()
{
    Config cfg = Config::createDefault();
    cfg.setup.state = SetupConfig::State::Done;
    const std::string stored = cfg.toJson(false, false);
    TEST_ASSERT_FALSE(Config::storedBeforeSetup(stored));
    Config loaded = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson(stored, loaded, false));
    TEST_ASSERT_EQUAL(SetupConfig::State::Done, loaded.setup.state);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_new_at_the_first_page);
    RUN_TEST(test_each_state_round_trips);
    RUN_TEST(test_an_unknown_state_is_refused_from_the_ui);
    RUN_TEST(test_a_step_out_of_range_is_refused_from_the_ui);
    RUN_TEST(test_a_bad_stored_value_keeps_the_current_one);
    RUN_TEST(test_a_save_without_setup_keeps_it);
    RUN_TEST(test_a_setup_only_save_changes_nothing_else);
    RUN_TEST(test_stored_before_setup_is_a_document_without_the_section);
    RUN_TEST(test_marking_stored_before_setup_offers_the_wizard);
    RUN_TEST(test_the_stored_document_carries_setup);
    return UNITY_END();
}
