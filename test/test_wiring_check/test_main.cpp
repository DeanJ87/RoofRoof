// The wiring check's verdicts (spec 023 FR-016, FR-018, FR-019, research R6,
// R7, R10): every check, level and boundary.

#include <cmath>
#include <cstring>
#include <unity.h>

#include "WiringCheck.h"

using namespace SQM::Board;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    WiringInputs good()
    {
        WiringInputs in;
        in.en = EnProbe::Low;
        in.hallCounts = 2000;
        in.supplyVolts = 12.4f;
        in.supplyLimit = 11.0f;
        in.fittedSwitches = 2;
        in.parkA = SwitchLevel::Closed;
        in.parkB = SwitchLevel::Open;
        in.currentSense = true;
        in.currentAmps[0] = 0.2f;
        in.currentAmps[1] = 0.1f;
        in.ethernet.mode = EthernetFacts::Mode::Probed;
        return in;
    }

    const Check &find(const WiringResult &result, CheckId id)
    {
        for (const Check &check : result.checks)
            if (check.id == id)
                return check;
        TEST_FAIL_MESSAGE("check missing");
        return result.checks.front();
    }

    Level levelOf(const WiringInputs &in, CheckId id)
    {
        const WiringResult result = evaluate(in);
        return find(result, id).level;
    }

    void assertLevel(Level expected, const WiringInputs &in, CheckId id)
    {
        TEST_ASSERT_EQUAL_STRING(levelName(expected), levelName(levelOf(in, id)));
    }
} // namespace

void test_all_good_passes_and_lists_every_check_in_order(void)
{
    const WiringResult result = evaluate(good());
    const CheckId order[] = {
        CheckId::EnPulldown,
        CheckId::Hall,
        CheckId::Supply,
        CheckId::ParkA,
        CheckId::ParkB,
        CheckId::CurrentR,
        CheckId::CurrentL,
        CheckId::Ethernet,
        CheckId::Motion};
    TEST_ASSERT_EQUAL_size_t(9, result.checks.size());
    for (size_t i = 0; i < 9; ++i)
    {
        TEST_ASSERT_EQUAL_STRING(checkIdName(order[i]), checkIdName(result.checks[i].id));
        TEST_ASSERT_TRUE(!result.checks[i].text.empty());
        if (order[i] != CheckId::Motion)
            TEST_ASSERT_EQUAL_STRING("pass", levelName(result.checks[i].level));
    }
    TEST_ASSERT_FALSE(result.suggestWt32);
}

void test_en_pulldown(void)
{
    WiringInputs in = good();
    assertLevel(Level::Pass, in, CheckId::EnPulldown);
    in.en = EnProbe::High;
    assertLevel(Level::Fail, in, CheckId::EnPulldown);
    in.en = EnProbe::NotRun;
    assertLevel(Level::Skipped, in, CheckId::EnPulldown);
    in.en = EnProbe::Busy;
    assertLevel(Level::Skipped, in, CheckId::EnPulldown);
    TEST_ASSERT_NOT_NULL(std::strstr(evaluate(in).checks.front().text.c_str(), "busy"));
}

void test_hall_rails_and_range(void)
{
    WiringInputs in = good();
    in.hallCounts = 30; // the low rail limit itself
    assertLevel(Level::Fail, in, CheckId::Hall);
    in.hallCounts = 31;
    assertLevel(Level::Warn, in, CheckId::Hall); // below the threshold range
    in.hallCounts = 99;
    assertLevel(Level::Warn, in, CheckId::Hall);
    in.hallCounts = 100;
    assertLevel(Level::Pass, in, CheckId::Hall);
    in.hallCounts = 3995;
    assertLevel(Level::Pass, in, CheckId::Hall);
    in.hallCounts = 3996;
    assertLevel(Level::Warn, in, CheckId::Hall);
    in.hallCounts = 4064;
    assertLevel(Level::Warn, in, CheckId::Hall);
    in.hallCounts = 4065;
    assertLevel(Level::Fail, in, CheckId::Hall);
    in.hallCounts = 4095;
    const WiringResult result = evaluate(in);
    TEST_ASSERT_EQUAL_FLOAT(4095.0f, find(result, CheckId::Hall).value);
    TEST_ASSERT_EQUAL_STRING("counts", find(result, CheckId::Hall).unit);
}

void test_supply(void)
{
    WiringInputs in = good();
    in.supplyVolts = NAN;
    assertLevel(Level::Fail, in, CheckId::Supply);
    in.supplyVolts = 0.3f;
    assertLevel(Level::Fail, in, CheckId::Supply);
    in.supplyVolts = 2.99f;
    assertLevel(Level::Fail, in, CheckId::Supply);
    in.supplyVolts = 3.0f;
    assertLevel(Level::Warn, in, CheckId::Supply); // below the limit
    in.supplyVolts = 10.99f;
    assertLevel(Level::Warn, in, CheckId::Supply);
    in.supplyVolts = 11.0f;
    assertLevel(Level::Pass, in, CheckId::Supply);
    in.supplyVolts = 15.0f;
    assertLevel(Level::Pass, in, CheckId::Supply);
    in.supplyVolts = 15.1f;
    assertLevel(Level::Warn, in, CheckId::Supply);
}

void test_park_inputs(void)
{
    WiringInputs in = good();
    in.parkA = SwitchLevel::Unreadable;
    assertLevel(Level::Warn, in, CheckId::ParkA);
    in.parkA = SwitchLevel::Open;
    assertLevel(Level::Pass, in, CheckId::ParkA);
    in.fittedSwitches = 1;
    assertLevel(Level::Skipped, in, CheckId::ParkB);
    in.fittedSwitches = 0;
    assertLevel(Level::Skipped, in, CheckId::ParkA);
    const WiringResult result = evaluate(good());
    TEST_ASSERT_NOT_NULL(std::strstr(find(result, CheckId::ParkA).text.c_str(), "Closed"));
    TEST_ASSERT_NOT_NULL(std::strstr(find(result, CheckId::ParkB).text.c_str(), "Open"));
}

void test_current_sense_at_rest(void)
{
    WiringInputs in = good();
    in.currentAmps[0] = 0.99f;
    assertLevel(Level::Pass, in, CheckId::CurrentR);
    in.currentAmps[0] = 1.0f;
    assertLevel(Level::Warn, in, CheckId::CurrentR);
    in.currentAmps[1] = NAN;
    assertLevel(Level::Warn, in, CheckId::CurrentL);
    // Current sense off: the pin's millivolts.
    in.currentSense = false;
    in.currentMillivolts[0] = 79.0f;
    in.currentMillivolts[1] = 80.0f;
    assertLevel(Level::Pass, in, CheckId::CurrentR);
    assertLevel(Level::Warn, in, CheckId::CurrentL);
    in.currentMillivolts[0] = NAN;
    assertLevel(Level::Skipped, in, CheckId::CurrentR);
    in.currentMillivolts[0] = 20.0f;
    const WiringResult result = evaluate(in);
    TEST_ASSERT_EQUAL_STRING("mV", find(result, CheckId::CurrentR).unit);
}

void test_ethernet_running(void)
{
    WiringInputs in = good();
    in.ethernet = EthernetFacts{};
    in.ethernet.mode = EthernetFacts::Mode::Running;
    assertLevel(Level::Fail, in, CheckId::Ethernet); // didn't start
    in.ethernet.started = true;
    in.ethernet.phyFound = true;
    assertLevel(Level::Warn, in, CheckId::Ethernet); // no link
    in.ethernet.link = true;
    in.ethernet.speedMbps = 100;
    assertLevel(Level::Warn, in, CheckId::Ethernet); // no address yet
    in.ethernet.hasAddress = true;
    assertLevel(Level::Pass, in, CheckId::Ethernet);
    TEST_ASSERT_FALSE(evaluate(in).suggestWt32);
}

void test_ethernet_probe(void)
{
    WiringInputs in = good();
    in.ethernet.mode = EthernetFacts::Mode::Probed;
    in.ethernet.idRead = true;
    in.ethernet.clockSeen = true;
    in.ethernet.id1 = 0x0007;
    in.ethernet.id2 = 0xC0F1;
    WiringResult result = evaluate(in);
    TEST_ASSERT_TRUE(result.suggestWt32);
    TEST_ASSERT_EQUAL_STRING("warn", levelName(find(result, CheckId::Ethernet).level));
    in.ethernet.id1 = 0x0022;
    in.ethernet.id2 = 0x1555;
    result = evaluate(in);
    TEST_ASSERT_FALSE(result.suggestWt32);
    TEST_ASSERT_NOT_NULL(std::strstr(find(result, CheckId::Ethernet).text.c_str(), "0022:1555"));
    in.ethernet.idRead = false;                      // the turnaround wasn't driven: nothing answered
    assertLevel(Level::Warn, in, CheckId::Ethernet); // a clock but no PHY
    in.ethernet.clockSeen = false;
    assertLevel(Level::Pass, in, CheckId::Ethernet); // a DevKit
    TEST_ASSERT_FALSE(evaluate(in).suggestWt32);
}

void test_ethernet_probe_skipped(void)
{
    WiringInputs in = good();
    in.ethernet = EthernetFacts{};
    in.ethernet.mode = EthernetFacts::Mode::SkippedPin;
    in.ethernet.usedPin = 16;
    WiringResult result = evaluate(in);
    TEST_ASSERT_EQUAL_STRING("skipped", levelName(find(result, CheckId::Ethernet).level));
    TEST_ASSERT_NOT_NULL(std::strstr(find(result, CheckId::Ethernet).text.c_str(), "GPIO16"));
    in.ethernet.mode = EthernetFacts::Mode::SkippedPsram;
    assertLevel(Level::Skipped, in, CheckId::Ethernet);
    in.ethernet.mode = EthernetFacts::Mode::SkippedWifiOnly;
    result = evaluate(in);
    TEST_ASSERT_NOT_NULL(std::strstr(find(result, CheckId::Ethernet).text.c_str(), "WiFi only"));
    in.ethernet.mode = EthernetFacts::Mode::SkippedBusy;
    assertLevel(Level::Skipped, in, CheckId::Ethernet);
    TEST_ASSERT_FALSE(evaluate(in).suggestWt32);
}

void test_motion_points_to_jog_and_teach(void)
{
    const WiringResult result = evaluate(good());
    const Check &motion = find(result, CheckId::Motion);
    TEST_ASSERT_EQUAL_STRING("skipped", levelName(motion.level));
    TEST_ASSERT_NOT_NULL(std::strstr(motion.text.c_str(), "Jog and teach"));
    TEST_ASSERT_TRUE(std::isnan(motion.value));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_good_passes_and_lists_every_check_in_order);
    RUN_TEST(test_en_pulldown);
    RUN_TEST(test_hall_rails_and_range);
    RUN_TEST(test_supply);
    RUN_TEST(test_park_inputs);
    RUN_TEST(test_current_sense_at_rest);
    RUN_TEST(test_ethernet_running);
    RUN_TEST(test_ethernet_probe);
    RUN_TEST(test_ethernet_probe_skipped);
    RUN_TEST(test_motion_points_to_jog_and_teach);
    return UNITY_END();
}
