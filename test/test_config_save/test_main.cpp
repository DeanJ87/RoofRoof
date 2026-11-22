// Spec 021: a save from a page loaded before the settings changed is refused
// only when a field it changes (or a credential partner it sends) changed on
// the device meanwhile; saves without a base revision are unchecked.

#include <string>
#include <unity.h>

#include "Config.h"
#include "ConfigSave.h"

using namespace SQM;
using ConfigSave::checkSave;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t REV = 41;

    const char *CURRENT = R"({"deviceName":"roof","wifi":{"ssid":"New","password":"********","ipv6":true},)"
                          R"("park":{"nina":{"url":"http://nina:1888"},"switches":1},"roof":{"travelMm":1850,"speedMmPerSecond":38.5},)"
                          R"("list":[1,2,3],"ntp":{"server1":"pool.ntp.org"}})";

    std::string body(const std::string &patch, const std::string &base, const char *revision = "40")
    {
        return std::string("{") + patch + R"(,"baseRevision":)" + revision + (base.empty() ? "" : R"(,"baseValues":)" + base) + "}";
    }
} // namespace

void test_no_base_revision_is_not_checked(void)
{
    const auto c = checkSave(R"({"wifi":{"ipv6":false}})", REV, CURRENT);
    TEST_ASSERT_FALSE(c.stale);
    TEST_ASSERT_EQUAL(0, c.conflicts.size());
    TEST_ASSERT_FALSE(checkSave(R"({"wifi":{"ipv6":false},"baseValues":{"wifi":{"ipv6":false}}})", REV, CURRENT).stale);
}

void test_current_revision_saves_without_compare(void)
{
    TEST_ASSERT_FALSE(checkSave(body(R"("wifi":{"ipv6":false})", R"({"wifi":{"ssid":"Old"}})", "41"), REV, CURRENT).stale);
}

void test_other_revision_saves_when_its_fields_are_unchanged(void)
{
    // The bench: IPv6 off from a page loaded before the network and N.I.N.A. changed.
    const auto c = checkSave(body(R"("wifi":{"ipv6":false})", R"({"wifi":{"ipv6":true}})"), REV, CURRENT);
    TEST_ASSERT_FALSE(c.stale);
}

void test_changed_fields_are_named(void)
{
    const auto c = checkSave(
        body(R"("park":{"nina":{"url":"http://other"}})", R"({"park":{"nina":{"url":""}},"wifi":{"ssid":"Old","password":"********"}})"),
        REV,
        CURRENT);
    TEST_ASSERT_TRUE(c.stale);
    TEST_ASSERT_EQUAL(2, c.conflicts.size());
    TEST_ASSERT_EQUAL_STRING("park.nina.url", c.conflicts[0].c_str());
    TEST_ASSERT_EQUAL_STRING("wifi.ssid", c.conflicts[1].c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Settings changed on the device since this page loaded them: park.nina.url, wifi.ssid", ConfigSave::conflictText(c).c_str());
}

void test_numbers_by_value_null_equals_missing_masks_compare_as_masks(void)
{
    TEST_ASSERT_FALSE(
        checkSave(body(R"("roof":{"travelMm":1900})", R"({"roof":{"travelMm":1850.0,"speedMmPerSecond":38.5}})"), REV, CURRENT).stale);
    TEST_ASSERT_FALSE(checkSave(body(R"("daylight":{"mode":"alert"})", R"({"daylight":{"mode":null},"x":null})"), REV, CURRENT).stale);
    TEST_ASSERT_FALSE(checkSave(body(R"("wifi":{"password":"pw"})", R"({"wifi":{"password":"********","ssid":"New"}})"), REV, CURRENT).stale);
    // A secret set where the page saw none.
    const auto c = checkSave(body(R"("wifi":{"password":"pw"})", R"({"wifi":{"password":""}})"), REV, CURRENT);
    TEST_ASSERT_TRUE(c.stale);
    TEST_ASSERT_EQUAL_STRING("wifi.password", c.conflicts[0].c_str());
    // Types differ: a conflict, never a silent match.
    TEST_ASSERT_TRUE(checkSave(body(R"("park":{"switches":2})", R"({"park":{"switches":"1"}})"), REV, CURRENT).stale);
    TEST_ASSERT_TRUE(checkSave(body(R"("deviceName":"x")", R"({"deviceName":null})"), REV, CURRENT).stale);
}

void test_arrays_and_sections_compare_whole(void)
{
    TEST_ASSERT_FALSE(checkSave(body(R"("list":[1])", R"({"list":[1,2,3]})"), REV, CURRENT).stale);
    TEST_ASSERT_TRUE(checkSave(body(R"("list":[1])", R"({"list":[1,2]})"), REV, CURRENT).stale);
    // A base leaf where the device has a section.
    TEST_ASSERT_TRUE(checkSave(body(R"("ntp":1)", R"({"ntp":"x"})"), REV, CURRENT).stale);
    // A section the device doesn't have: its non-null leaves changed.
    const auto c = checkSave(body(R"("alerts":{})", R"({"alerts":{"ntfy":{"server":"a","topic":null}}})"), REV, CURRENT);
    TEST_ASSERT_EQUAL(1, c.conflicts.size());
    TEST_ASSERT_EQUAL_STRING("alerts.ntfy.server", c.conflicts[0].c_str());
}

void test_without_base_values_or_with_a_bad_revision_it_is_stale(void)
{
    const auto c = checkSave(body(R"("wifi":{"ipv6":false})", ""), REV, CURRENT);
    TEST_ASSERT_TRUE(c.stale);
    TEST_ASSERT_EQUAL(0, c.conflicts.size());
    TEST_ASSERT_TRUE(checkSave(body(R"("wifi":{"ipv6":false})", "", R"("41")"), REV, CURRENT).stale);
    TEST_ASSERT_TRUE(checkSave(body(R"("wifi":{"ipv6":false})", "", "-1"), REV, CURRENT).stale);
    TEST_ASSERT_TRUE(checkSave(body(R"("wifi":{"ipv6":false})", "[]"), REV, CURRENT).stale);
    // Settings the device can't read back: refused, never saved blind.
    TEST_ASSERT_TRUE(checkSave(body(R"("wifi":{"ipv6":false})", R"({"wifi":{"ipv6":true}})"), REV, "{").stale);
}

void test_at_most_sixteen_names(void)
{
    std::string base = "{";
    for (int i = 0; i < 20; ++i)
        base += std::string(i ? "," : "") + "\"k" + std::to_string(i) + "\":1";
    base += "}";
    const auto c = checkSave(body(R"("deviceName":"x")", base), REV, CURRENT);
    TEST_ASSERT_TRUE(c.stale);
    TEST_ASSERT_EQUAL(ConfigSave::MAX_CONFLICTS, c.conflicts.size());
}

void test_not_an_object_is_left_to_the_settings_checks(void)
{
    TEST_ASSERT_FALSE(checkSave("[1]", REV, CURRENT).stale);
    TEST_ASSERT_FALSE(checkSave("{", REV, CURRENT).stale);
}

// The two keys never reach the settings: the device ignores keys it doesn't know.
void test_base_keys_do_not_change_settings(void)
{
    const Config base = Config::createDefault();
    std::string error;
    const auto saved = Config::fromJson(body(R"("deviceName":"obs")", R"({"deviceName":"roofroof"})"), &base, &error);
    TEST_ASSERT_TRUE_MESSAGE(saved.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("obs", saved->deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING(base.wifi.ssid.c_str(), saved->wifi.ssid.c_str());
}

// Against the real settings document: what GET /api/config shows compares equal.
void test_round_trip_of_the_device_document(void)
{
    const Config cfg = Config::createDefault();
    const std::string shown = cfg.toJson(true);
    const std::string sent = std::string(R"({"deviceName":"x","baseRevision":1,"baseValues":)") + shown + "}";
    const auto c = checkSave(sent, 2, shown);
    TEST_ASSERT_FALSE_MESSAGE(c.stale, ConfigSave::conflictText(c).c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_base_revision_is_not_checked);
    RUN_TEST(test_current_revision_saves_without_compare);
    RUN_TEST(test_other_revision_saves_when_its_fields_are_unchanged);
    RUN_TEST(test_changed_fields_are_named);
    RUN_TEST(test_numbers_by_value_null_equals_missing_masks_compare_as_masks);
    RUN_TEST(test_arrays_and_sections_compare_whole);
    RUN_TEST(test_without_base_values_or_with_a_bad_revision_it_is_stale);
    RUN_TEST(test_at_most_sixteen_names);
    RUN_TEST(test_not_an_object_is_left_to_the_settings_checks);
    RUN_TEST(test_base_keys_do_not_change_settings);
    RUN_TEST(test_round_trip_of_the_device_document);
    return UNITY_END();
}
