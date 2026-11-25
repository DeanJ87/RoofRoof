#include <unity.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>

#include "Config.h"

using namespace SQM;

// Settings saved by every earlier release load without losing anything that
// release stored (SQMeter spec 011 FR-005 / SC-003; constitution, Platform
// Constraints). The fixtures in test/fixtures/config-releases are what each
// tagged release's own config code wrote for the same set-up device (WiFi,
// MQTT, auth changed from the defaults); tools/config-releases/build.sh adds
// one per release. Before the first release (v0.1.0) there are none.

namespace
{
    std::string readFile(const std::string &path)
    {
        std::ifstream in(path);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    void assertSame(JsonVariantConst stored, JsonVariantConst loaded, const std::string &path)
    {
        if (stored.is<JsonObjectConst>())
        {
            for (JsonPairConst field : stored.as<JsonObjectConst>())
                assertSame(field.value(), loaded[field.key()], path + "." + field.key().c_str());
            return;
        }
        if (stored.is<JsonArrayConst>())
            return; // lists (e.g. publish groups) are covered by their own tests
        const std::string message = "changed on load: " + path;
        if (stored.is<float>() && !stored.is<long long>())
        {
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4F, stored.as<float>(), loaded.as<float>(), message.c_str());
            return;
        }
        TEST_ASSERT_EQUAL_STRING_MESSAGE(stored.as<std::string>().c_str(), loaded.as<std::string>().c_str(), message.c_str());
    }

    // Settings the current firmware deliberately renamed, migrated or no
    // longer stores; each is checked by its own test elsewhere.
    bool migrated(const std::string &path)
    {
        // None yet: list a path here, with why, when a release renames or
        // drops a setting.
        static const char *const PATHS[] = {""};
        for (const char *p : PATHS)
            if (path == p)
                return true;
        return false;
    }

    void assertKept(JsonObjectConst stored, JsonObjectConst loaded, const std::string &prefix = "")
    {
        for (JsonPairConst field : stored)
        {
            const std::string path = prefix + "." + field.key().c_str();
            if (migrated(path))
                continue;
            if (field.value().is<JsonObjectConst>())
                assertKept(field.value().as<JsonObjectConst>(), loaded[field.key()].as<JsonObjectConst>(), path);
            else
                assertSame(field.value(), loaded[field.key()], path);
        }
    }

    void loadRelease(const std::string &path)
    {
        const char *tag = path.c_str();
        const std::string stored = readFile(path);
        TEST_ASSERT_FALSE_MESSAGE(stored.empty(), tag);

        Config loaded = Config::createDefault();
        std::string reason;
        const bool ok = Config::applyJson(stored, loaded, false, &reason);
        TEST_ASSERT_TRUE_MESSAGE(ok, (std::string(tag) + " rejected: " + reason).c_str());

        DynamicJsonDocument before(16384);
        DynamicJsonDocument after(16384);
        TEST_ASSERT_FALSE(deserializeJson(before, stored));
        TEST_ASSERT_FALSE(deserializeJson(after, loaded.toJson(false)));
        assertKept(before.as<JsonObjectConst>(), after.as<JsonObjectConst>());

        // Spot checks that matter most: still on the network, still protected.
        TEST_ASSERT_EQUAL_STRING("HomeNet", loaded.wifi.ssid.c_str());
        TEST_ASSERT_EQUAL_STRING("secret-wifi", loaded.wifi.password.c_str());
        TEST_ASSERT_TRUE(loaded.auth.enabled);
        TEST_ASSERT_EQUAL_STRING("adminpass", loaded.auth.password.c_str());
        TEST_ASSERT_TRUE(loaded.mqtt.enabled);
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_every_release_loads_without_loss()
{
    const std::filesystem::path dir("test/fixtures/config-releases");
    TEST_ASSERT_TRUE_MESSAGE(std::filesystem::is_directory(dir), "run from the project root");
    std::vector<std::string> releases;
    for (const auto &entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".json")
            releases.push_back(entry.path().string());
    std::sort(releases.begin(), releases.end());
    for (const std::string &release : releases)
        loadRelease(release);
    if (releases.empty())
        TEST_MESSAGE("no release fixtures yet: the first comes with v0.1.0");
}

// A stored config from before the roof settings existed (no "roof") loads
// with the roof defaults and current sensing off.
void test_stored_config_without_roof_loads_with_roof_defaults()
{
    Config loaded = Config::createDefault();
    std::string reason;
    const std::string stored = "{\"deviceName\":\"old\",\"wifi\":{\"ssid\":\"HomeNet\",\"password\":\"secret-wifi\"}}";
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(stored, loaded, false, &reason), reason.c_str());
    const RoofConfig defaults;
    TEST_ASSERT_EQUAL_UINT32(defaults.travelMm, loaded.roof.travelMm);
    TEST_ASSERT_EQUAL_UINT32(defaults.slowDutyPercent, loaded.roof.slowDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(defaults.hallHighCounts, loaded.roof.hallHighCounts);
    TEST_ASSERT_FALSE(loaded.roof.currentSenseEnabled);
    TEST_ASSERT_EQUAL_STRING("HomeNet", loaded.wifi.ssid.c_str());
}

// Spec 015 (FR-002, SC-004): settings stored before the setup wizard load
// as "offered" (Config::load marks them), every other setting as stored.
void test_stored_config_without_setup_is_offered_the_wizard()
{
    const std::string stored = "{\"deviceName\":\"old\",\"wifi\":{\"ssid\":\"HomeNet\",\"password\":\"secret-wifi\"},"
                               "\"roof\":{\"travelMm\":2100}}";
    Config loaded = Config::createDefault();
    std::string reason;
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(stored, loaded, false, &reason), reason.c_str());
    TEST_ASSERT_TRUE(Config::storedBeforeSetup(stored));
    loaded.markStoredBeforeSetup();
    TEST_ASSERT_EQUAL(SetupConfig::State::Offered, loaded.setup.state);
    TEST_ASSERT_EQUAL_UINT8(0, loaded.setup.step);
    TEST_ASSERT_EQUAL_UINT32(2100, loaded.roof.travelMm);
    TEST_ASSERT_EQUAL_STRING("old", loaded.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING("HomeNet", loaded.wifi.ssid.c_str());
    // Every release fixture predates the wizard.
    const std::filesystem::path dir("test/fixtures/config-releases");
    if (std::filesystem::is_directory(dir))
        for (const auto &entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".json")
                TEST_ASSERT_TRUE_MESSAGE(Config::storedBeforeSetup(readFile(entry.path().string())), entry.path().string().c_str());
}

// A stored MQTT section from before spec 008 (no "roofCommands") loads with
// MQTT roof commands off, whatever else was on (constitution: a migration
// never turns on a motion feature).
void test_stored_mqtt_without_roof_commands_loads_them_off()
{
    std::string reason;
    const std::string stored =
        "{\"mqtt\":{\"enabled\":true,\"broker\":\"10.0.0.2\",\"topic\":\"roofroof\",\"homeAssistant\":{\"enabled\":true}}}";
    Config fresh = Config::createDefault();
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(stored, fresh, false, &reason), reason.c_str());
    TEST_ASSERT_TRUE(fresh.mqtt.enabled);
    TEST_ASSERT_TRUE(fresh.mqtt.homeAssistant);
    TEST_ASSERT_FALSE(fresh.mqtt.roofCommands);
}

// A stored roof section from before the current-sense scale existed (spec 002)
// loads with its default, and current sensing stays as it was (off).
void test_stored_roof_without_current_scale_loads_with_its_default()
{
    Config loaded = Config::createDefault();
    std::string reason;
    const std::string stored = "{\"roof\":{\"travelMm\":2000,\"currentSenseEnabled\":false,\"maxCurrentAmps\":12}}";
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(stored, loaded, false, &reason), reason.c_str());
    TEST_ASSERT_EQUAL_UINT32(2000, loaded.roof.travelMm);
    TEST_ASSERT_EQUAL_FLOAT(RoofConfig().currentAmpsPerVolt, loaded.roof.currentAmpsPerVolt);
    TEST_ASSERT_EQUAL_FLOAT(8.5f, loaded.roof.currentAmpsPerVolt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loaded.roof.minRunningAmps); // the broken-wire check stays off
    TEST_ASSERT_FALSE(loaded.roof.currentSenseEnabled);
}

// Spec 016 fix (2026-11-24): settings saved before the measured slow speed load
// it as 0 (not measured: moves at the slow duty timed as before); a measured one
// round-trips, and one out of range is refused with the reason.
void test_stored_roof_without_slow_speed_loads_it_unmeasured()
{
    Config loaded = Config::createDefault();
    std::string reason;
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson("{\"roof\":{\"speedMmPerSecond\":40}}", loaded, false, &reason), reason.c_str());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, loaded.roof.slowSpeedMmPerSecond);
    TEST_ASSERT_EQUAL_UINT32(0, loaded.roof.slowSpeedDutyPercent);
    TEST_ASSERT_TRUE_MESSAGE(
        Config::applyJson("{\"roof\":{\"slowSpeedMmPerSecond\":9.4,\"slowSpeedDutyPercent\":40}}", loaded, false, &reason), reason.c_str());
    const auto again = Config::fromJson(loaded.toJson(false));
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL_FLOAT(9.4f, again->roof.slowSpeedMmPerSecond);
    TEST_ASSERT_EQUAL_UINT32(40, again->roof.slowSpeedDutyPercent);
    TEST_ASSERT_FALSE(Config::applyJson("{\"roof\":{\"slowSpeedDutyPercent\":9}}", loaded, false, &reason));
    TEST_ASSERT_EQUAL_STRING("Roof: the slow speed's duty must be 0 (none) or between 10 and 100 %", reason.c_str());
    for (const char *bad : {"0.5", "500.5", "-1"})
    {
        Config c = Config::createDefault();
        TEST_ASSERT_FALSE(Config::applyJson(std::string("{\"roof\":{\"slowSpeedMmPerSecond\":") + bad + "}}", c, false, &reason));
        TEST_ASSERT_EQUAL_STRING("Roof: slow speed must be 0 (not measured) or between 1 and 500 mm/s", reason.c_str());
    }
}

// Spec 030 (owner decision, constitution 8.0.0): the blind close is on after an
// upgrade, not only for a new device; the N.I.N.A.-lost close stays off.
void test_stored_settings_without_spec_030_load_blind_close_on_and_nina_lost_off()
{
    Config loaded = Config::createDefault();
    std::string reason;
    const std::string stored = "{\"roof\":{\"travelMm\":2000},\"autoClose\":{\"remindMinutes\":30}}";
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(stored, loaded, false, &reason), reason.c_str());
    TEST_ASSERT_TRUE(loaded.roof.blindClose);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, loaded.roof.blindSagVolts);
    TEST_ASSERT_FALSE(loaded.autoClose.ninaLostClose);
    TEST_ASSERT_EQUAL_UINT32(5, loaded.autoClose.ninaLostMinutes);
    TEST_ASSERT_TRUE_MESSAGE(
        Config::applyJson(
            "{\"roof\":{\"blindClose\":false,\"blindSagVolts\":3.5},\"autoClose\":{\"ninaLostClose\":true,\"ninaLostMinutes\":12}}",
            loaded,
            false,
            &reason),
        reason.c_str());
    const auto again = Config::fromJson(loaded.toJson(false));
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_FALSE(again->roof.blindClose);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, again->roof.blindSagVolts);
    TEST_ASSERT_TRUE(again->autoClose.ninaLostClose);
    TEST_ASSERT_EQUAL_UINT32(12, again->autoClose.ninaLostMinutes);
    TEST_ASSERT_FALSE(Config::applyJson("{\"roof\":{\"blindSagVolts\":0.4}}", loaded, false, &reason));
    TEST_ASSERT_EQUAL_STRING("Roof: blind close stall sag must be between 0.5 and 6 volts", reason.c_str());
    Config other = Config::createDefault();
    TEST_ASSERT_FALSE(Config::applyJson("{\"autoClose\":{\"ninaLostMinutes\":61}}", other, false, &reason));
    TEST_ASSERT_EQUAL_STRING("Automatic close: N.I.N.A. lost time must be between 1 and 60 minutes", reason.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_stored_settings_without_spec_030_load_blind_close_on_and_nina_lost_off);
    RUN_TEST(test_every_release_loads_without_loss);
    RUN_TEST(test_stored_config_without_roof_loads_with_roof_defaults);
    RUN_TEST(test_stored_roof_without_current_scale_loads_with_its_default);
    RUN_TEST(test_stored_mqtt_without_roof_commands_loads_them_off);
    RUN_TEST(test_stored_config_without_setup_is_offered_the_wizard);
    RUN_TEST(test_stored_roof_without_slow_speed_loads_it_unmeasured);
    return UNITY_END();
}
