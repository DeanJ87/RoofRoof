// The settings' pins group and Wi-Fi's role beside Ethernet (spec 023 FR-001
// to FR-005, FR-022, FR-029): defaults, an upgrade from settings without them,
// presets, the rules on a save, and stored pins that break a rule still loading
// (the device then holds every motor pin at boot).

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"
#include "ConfigPins.h"

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

    // As Config::load does: stored JSON over the defaults, not strict.
    bool loadStored(const std::string &json, Config &out, std::string &error)
    {
        out = Config::createDefault();
        return Config::applyJson(json, out, false, &error);
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_a_new_device_has_no_board_chosen()
{
    const Config c = Config::createDefault();
    TEST_ASSERT_TRUE(c.pins.preset == Board::Preset::Unset);
    TEST_ASSERT_TRUE(c.network.mode == NetworkConfig::Mode::Auto);
    TEST_ASSERT_TRUE(c.validatePins(false)); // saving other settings first is fine
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(c.validate(&error), error.c_str());
    DynamicJsonDocument doc(16384);
    deserializeJson(doc, c.toJson());
    TEST_ASSERT_EQUAL_STRING("unset", doc["pins"]["preset"]);
    TEST_ASSERT_EQUAL_STRING("auto", doc["network"]["mode"]);
    // A new device's stored settings keep "unset" when read back.
    Config back;
    TEST_ASSERT_TRUE(loadStored(c.toJson(), back, error));
    TEST_ASSERT_TRUE(back.pins.preset == Board::Preset::Unset);
}

void test_devkit_preset_json()
{
    const Config c = applied(R"({"pins":{"preset":"devkit"}})");
    DynamicJsonDocument doc(16384);
    deserializeJson(doc, c.toJson());
    TEST_ASSERT_EQUAL_STRING("devkit", doc["pins"]["preset"]);
    TEST_ASSERT_EQUAL_STRING("esp32-devkit", doc["pins"]["board"]);
    TEST_ASSERT_EQUAL_INT(25, doc["pins"]["rpwm"]);
    TEST_ASSERT_EQUAL_INT(27, doc["pins"]["enable"]);
    TEST_ASSERT_EQUAL_INT(33, doc["pins"]["parkB"]);
}

void test_settings_saved_before_spec_023_load_as_devkit()
{
    // An older document: no pins group, no network mode, a rain contact that is now dropped.
    Config c;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(
        loadStored(R"({"deviceName":"Old","safety":{"staleSeconds":60,"rainContact":true},"wifi":{"ssid":"home"}})", c, error),
        error.c_str());
    TEST_ASSERT_TRUE(c.pins == Board::presetMap(Board::Preset::Devkit)); // migrated explicitly
    TEST_ASSERT_TRUE(c.network.mode == NetworkConfig::Mode::Auto);
    TEST_ASSERT_EQUAL_UINT32(60, c.safety.staleSeconds);
    TEST_ASSERT_TRUE(c.toJson().find("rainContact") == std::string::npos);
}

void test_a_named_preset_alone_fills_its_pins()
{
    const Config c = applied(R"({"pins":{"preset":"wt32-carrier-v1"}})");
    TEST_ASSERT_TRUE(c.pins == Board::presetMap(Board::Preset::Wt32CarrierV1));
    TEST_ASSERT_TRUE(c.pins.board == Board::BoardId::Wt32Eth01);
    TEST_ASSERT_EQUAL_UINT8(4, c.pins.enable);
    // And back.
    Config back = c;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(R"({"pins":{"preset":"devkit"}})", back, true, &error), error.c_str());
    TEST_ASSERT_TRUE(back.pins == Board::presetMap(Board::Preset::Devkit));
}

void test_a_named_preset_with_other_pins_is_refused()
{
    const std::string reason = rejectReason(R"({"pins":{"preset":"devkit","rpwm":13}})");
    TEST_ASSERT_EQUAL_STRING("RPWM on GPIO13: this preset's pins are fixed; choose Custom to change one", reason.c_str());
    // A pin alone on a named preset, the same.
    TEST_ASSERT_TRUE(rejectReason(R"({"pins":{"preset":"devkit"}})").empty());
    const Config devkit = applied(R"({"pins":{"preset":"devkit"}})");
    std::string error;
    Config changed = devkit;
    TEST_ASSERT_FALSE(Config::applyJson(R"({"pins":{"rpwm":13}})", changed, true, &error));
    // The preset's own values are fine.
    applied(R"({"pins":{"preset":"devkit","rpwm":25,"enable":27}})");
}

void test_a_pin_alone_on_unset_is_ignored_until_a_preset_is_chosen()
{
    // Unset uses no pins; choosing Custom then checks them.
    const Config c = applied(R"({"pins":{"preset":"custom"}})");
    TEST_ASSERT_TRUE(c.pins.preset == Board::Preset::Custom);
}

void test_custom_is_checked_by_the_rules()
{
    const Config c = applied(R"({"pins":{"preset":"custom","rpwm":13,"lpwm":14}})");
    TEST_ASSERT_TRUE(c.pins.preset == Board::Preset::Custom);
    TEST_ASSERT_EQUAL_UINT8(13, c.pins.rpwm);
    TEST_ASSERT_EQUAL_UINT8(27, c.pins.enable); // kept
    TEST_ASSERT_EQUAL_STRING(
        "RPWM on GPIO35: GPIO35 is input only and can't drive an output",
        rejectReason(R"({"pins":{"preset":"custom","rpwm":35}})").c_str());
    TEST_ASSERT_EQUAL_STRING("EN on GPIO6: GPIO6 is a flash pin", rejectReason(R"({"pins":{"preset":"custom","enable":6}})").c_str());
    // A WT32 with the DevKit's pins: Ethernet's.
    TEST_ASSERT_TRUE(rejectReason(R"({"pins":{"preset":"custom","board":"wt32-eth01"}})").size() > 0);
}

void test_bad_names_and_values_are_refused()
{
    TEST_ASSERT_EQUAL_STRING(
        "Pins: preset must be devkit, wt32-carrier-v1 or custom", rejectReason(R"({"pins":{"preset":"wt32"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Pins: board must be esp32-devkit or wt32-eth01", rejectReason(R"({"pins":{"preset":"custom","board":"s3"}})").c_str());
    TEST_ASSERT_EQUAL_STRING("Pins: rpwm must be a GPIO number", rejectReason(R"({"pins":{"preset":"custom","rpwm":-1}})").c_str());
    TEST_ASSERT_EQUAL_STRING("Pins: rpwm must be a GPIO number", rejectReason(R"({"pins":{"preset":"custom","rpwm":300}})").c_str());
    TEST_ASSERT_EQUAL_STRING("Pins: rpwm must be a GPIO number", rejectReason(R"({"pins":{"preset":"custom","rpwm":"25"}})").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: pins must be an object", rejectReason(R"({"pins":7})").c_str());
}

void test_stored_pins_that_break_a_rule_still_load()
{
    Config c;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(loadStored(R"({"pins":{"preset":"custom","board":"esp32-devkit","enable":6}})", c, error), error.c_str());
    TEST_ASSERT_EQUAL_UINT8(6, c.pins.enable);
    TEST_ASSERT_FALSE(c.validatePins(false));
    // But such settings can't be saved again until the pins are fixed.
    TEST_ASSERT_FALSE(c.validate(&error));
    TEST_ASSERT_EQUAL_STRING("EN on GPIO6: GPIO6 is a flash pin", error.c_str());
}

void test_stored_pins_with_an_unknown_name_are_rejected()
{
    // The whole document fails: Config::load reports it and the boot holds the pins.
    Config c;
    std::string error;
    TEST_ASSERT_FALSE(loadStored(R"({"pins":{"preset":"devkit2"}})", c, error));
}

void test_psram_pins_are_checked_against_the_chip()
{
    Config c = applied(R"({"pins":{"preset":"custom","rpwm":17,"lpwm":13}})");
    TEST_ASSERT_TRUE(c.validatePins(false));
    std::string error;
    TEST_ASSERT_FALSE(c.validatePins(true, &error));
    TEST_ASSERT_EQUAL_STRING("RPWM on GPIO17: GPIO17 is used by this chip's PSRAM", error.c_str());
}

void test_network_mode_values()
{
    TEST_ASSERT_TRUE(applied(R"({"network":{"mode":"ethernet"}})").network.mode == NetworkConfig::Mode::Ethernet);
    TEST_ASSERT_TRUE(applied(R"({"network":{"mode":"wifi"}})").network.mode == NetworkConfig::Mode::Wifi);
    TEST_ASSERT_TRUE(applied(R"({"network":{"mode":"auto"}})").network.mode == NetworkConfig::Mode::Auto);
    TEST_ASSERT_EQUAL_STRING("Network: mode must be auto, ethernet or wifi", rejectReason(R"({"network":{"mode":"both"}})").c_str());
}

void test_round_trip_through_json()
{
    const Config c = applied(
        R"({"pins":{"preset":"custom","board":"wt32-eth01","rpwm":14,"lpwm":15,"enable":4,"currentR":35,"currentL":32,"supply":36,"hall":39,"parkA":33,"parkB":17},"network":{"mode":"ethernet"}})");
    Config back = Config::createDefault();
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(c.toJson(), back, false, &error), error.c_str());
    TEST_ASSERT_TRUE(back.pins == c.pins);
    TEST_ASSERT_TRUE(back.network.mode == NetworkConfig::Mode::Ethernet);
}

void test_config_pins_read_and_write()
{
    StaticJsonDocument<512> doc;
    ConfigPins::write(doc.to<JsonObject>(), Board::presetMap(Board::Preset::Wt32CarrierV1));
    TEST_ASSERT_EQUAL_STRING("wt32-carrier-v1", doc["preset"]);
    TEST_ASSERT_EQUAL_INT(5, doc["parkB"]);
    Board::PinMap map;
    std::string error;
    TEST_ASSERT_TRUE(ConfigPins::read(doc.as<JsonObjectConst>(), map, &error));
    TEST_ASSERT_TRUE(map == Board::presetMap(Board::Preset::Wt32CarrierV1));
}

void test_the_stored_size_with_pins_fits()
{
    Config c = Config::createDefault();
    c.pins = Board::presetMap(Board::Preset::Wt32CarrierV1);
    c.pins.preset = Board::Preset::Custom;
    TEST_ASSERT_TRUE(c.toJson(false, false).size() < Config::MAX_PERSISTED_JSON_BYTES);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_new_device_has_no_board_chosen);
    RUN_TEST(test_devkit_preset_json);
    RUN_TEST(test_a_pin_alone_on_unset_is_ignored_until_a_preset_is_chosen);
    RUN_TEST(test_settings_saved_before_spec_023_load_as_devkit);
    RUN_TEST(test_a_named_preset_alone_fills_its_pins);
    RUN_TEST(test_a_named_preset_with_other_pins_is_refused);
    RUN_TEST(test_custom_is_checked_by_the_rules);
    RUN_TEST(test_bad_names_and_values_are_refused);
    RUN_TEST(test_stored_pins_that_break_a_rule_still_load);
    RUN_TEST(test_stored_pins_with_an_unknown_name_are_rejected);
    RUN_TEST(test_psram_pins_are_checked_against_the_chip);
    RUN_TEST(test_network_mode_values);
    RUN_TEST(test_round_trip_through_json);
    RUN_TEST(test_config_pins_read_and_write);
    RUN_TEST(test_the_stored_size_with_pins_fits);
    return UNITY_END();
}
