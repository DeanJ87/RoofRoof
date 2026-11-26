#include <unity.h>

#include "HaDiscovery.h"

#include <map>
#include <string>

// Spec 048: Home Assistant discovery one config at a time after a (re)connect,
// and skipped when the broker kept this boot's set (its retained marker).

using SQM::HaDiscovery::Pacer;
namespace Ha = SQM::HaDiscovery;

namespace
{
    const Ha::Device DEVICE{"roofroof_0123456789ab", "RoofRoof", "1.0", "roofroof", "homeassistant"};

    std::string textOf(const JsonDocument *config)
    {
        std::string out;
        if (config != nullptr)
            serializeJson(*config, out);
        return out;
    }
} // namespace

void test_one_at_a_time_is_the_same_as_all_at_once()
{
    Ha::Groups groups;
    groups.roofCommands = true;
    std::map<std::string, std::string> all;
    Ha::forEach(DEVICE, groups, [&](const std::string &topic, const JsonDocument *config) { all[topic] = textOf(config); });
    TEST_ASSERT_EQUAL(Ha::count(), all.size());
    std::map<std::string, std::string> paced;
    DynamicJsonDocument doc(Ha::CONFIG_DOC_BYTES);
    for (size_t i = 0; i < Ha::count(); ++i)
        Ha::publishOne(
            i, DEVICE, groups, doc, [&](const std::string &topic, const JsonDocument *config) { paced[topic] = textOf(config); });
    TEST_ASSERT_TRUE(all == paced);
    // Past the end: nothing.
    bool called = false;
    Ha::publishOne(Ha::count(), DEVICE, groups, doc, [&](const std::string &, const JsonDocument *) { called = true; });
    TEST_ASSERT_FALSE(called);
}

void test_the_marker_round_trips_and_follows_the_key()
{
    const uint32_t a = Ha::markerOf("1RoofRooftopprefix100");
    const uint32_t b = Ha::markerOf("1RoofRooftopprefix110");
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NOT_EQUAL(0, a);
    char text[Ha::MARKER_TEXT_BYTES];
    Ha::markerText(a, text);
    TEST_ASSERT_EQUAL(8, strlen(text));
    TEST_ASSERT_EQUAL_UINT32(a, Ha::parseMarker(text, 8));
    Ha::markerText(0x0000ab01u, text);
    TEST_ASSERT_EQUAL_STRING("0000ab01", text);
    TEST_ASSERT_EQUAL_UINT32(0, Ha::parseMarker("0000ab0", 7));
    TEST_ASSERT_EQUAL_UINT32(0, Ha::parseMarker("0000AB01", 8)); // only what markerText writes
    TEST_ASSERT_EQUAL_UINT32(0, Ha::parseMarker("", 0));
}

void test_a_fresh_set_goes_out_one_config_per_gap_then_the_marker()
{
    Pacer pacer;
    TEST_ASSERT_EQUAL(Pacer::Kind::Done, pacer.next(0, 3).kind); // never started
    pacer.start(7, 1000, false);
    Pacer::Step s = pacer.next(1000, 3);
    TEST_ASSERT_EQUAL(Pacer::Kind::Entity, s.kind); // no marker to wait for
    TEST_ASSERT_EQUAL(0, s.index);
    pacer.done(s, 1000);
    TEST_ASSERT_EQUAL(Pacer::Kind::Wait, pacer.next(1000 + Pacer::GAP_MS - 1, 3).kind);
    s = pacer.next(1000 + Pacer::GAP_MS, 3);
    TEST_ASSERT_EQUAL(Pacer::Kind::Entity, s.kind);
    TEST_ASSERT_EQUAL(1, s.index);
    pacer.done(s, 1200);
    // Not carried out (the publish failed): the same one again.
    s = pacer.next(1400, 3);
    TEST_ASSERT_EQUAL(2, s.index);
    s = pacer.next(1500, 3);
    TEST_ASSERT_EQUAL(2, s.index);
    pacer.done(s, 1500);
    s = pacer.next(1700, 3);
    TEST_ASSERT_EQUAL(Pacer::Kind::Marker, s.kind);
    pacer.done(s, 1700);
    TEST_ASSERT_FALSE(pacer.running());
    TEST_ASSERT_FALSE(pacer.skipped());
    TEST_ASSERT_EQUAL(Pacer::Kind::Done, pacer.next(5000, 3).kind);
}

void test_a_kept_set_is_skipped_when_the_broker_hands_back_its_marker()
{
    Pacer pacer;
    pacer.start(7, 1000, true);
    TEST_ASSERT_EQUAL(Pacer::Kind::Wait, pacer.next(1100, 3).kind);
    pacer.markerSeen(8); // another set's: ignored
    TEST_ASSERT_EQUAL(Pacer::Kind::Wait, pacer.next(1200, 3).kind);
    pacer.markerSeen(7);
    const Pacer::Step s = pacer.next(1300, 3);
    TEST_ASSERT_EQUAL(Pacer::Kind::Skip, s.kind);
    pacer.done(s, 1300);
    TEST_ASSERT_TRUE(pacer.skipped());
    TEST_ASSERT_FALSE(pacer.running());
}

void test_a_broker_that_kept_nothing_gets_the_set_after_the_wait()
{
    Pacer pacer;
    pacer.start(7, 1000, true);
    TEST_ASSERT_EQUAL(Pacer::Kind::Wait, pacer.next(1000 + Pacer::MARKER_WAIT_MS - 1, 3).kind);
    Pacer::Step s = pacer.next(1000 + Pacer::MARKER_WAIT_MS, 3);
    TEST_ASSERT_EQUAL(Pacer::Kind::Entity, s.kind);
    pacer.done(s, 3000);
    pacer.markerSeen(7); // too late: publishing has started, it finishes
    s = pacer.next(3200, 3);
    TEST_ASSERT_EQUAL(Pacer::Kind::Entity, s.kind);
}

void test_an_untrusted_start_ignores_a_marker()
{
    // The first set this boot (or new settings): always sent whole.
    Pacer pacer;
    pacer.start(7, 1000, false);
    pacer.markerSeen(7);
    TEST_ASSERT_EQUAL(Pacer::Kind::Entity, pacer.next(1000, 3).kind);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_one_at_a_time_is_the_same_as_all_at_once);
    RUN_TEST(test_the_marker_round_trips_and_follows_the_key);
    RUN_TEST(test_a_fresh_set_goes_out_one_config_per_gap_then_the_marker);
    RUN_TEST(test_a_kept_set_is_skipped_when_the_broker_hands_back_its_marker);
    RUN_TEST(test_a_broker_that_kept_nothing_gets_the_set_after_the_wait);
    RUN_TEST(test_an_untrusted_start_ignores_a_marker);
    return UNITY_END();
}
