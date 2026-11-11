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

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_release_loads_without_loss);
    RUN_TEST(test_stored_config_without_roof_loads_with_roof_defaults);
    return UNITY_END();
}
