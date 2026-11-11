// The park rule (lib/SafetyInputLogic ParkRule): constitution II - parked only
// with N.I.N.A. AtPark AND every fitted park switch closed, both fresh; any
// disagreement or unreadable input is "not parked". Spec 004 US2.

#include <string>
#include <unity.h>

#include "NinaApi.h"
#include "ParkRule.h"

using namespace SQM::Safety;
using SQM::Nina::MountReading;
using SQM::Nina::ReadingStatus;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t NOW_MS = 500000;

    MountReading mount(bool connected, bool atPark, bool slewing, uint64_t takenAtMs = NOW_MS - 1000)
    {
        MountReading m;
        m.status = ReadingStatus::Good;
        m.connected = connected;
        m.atPark = atPark;
        m.slewing = slewing;
        m.takenAtMs = takenAtMs;
        return m;
    }

    ParkInputs parkedInputs(uint8_t fitted)
    {
        ParkInputs in;
        in.nina = mount(true, true, false);
        in.fittedSwitches = fitted;
        in.switch1 = SwitchReading::Closed;
        in.switch2 = SwitchReading::Closed;
        in.nowMs = NOW_MS;
        return in;
    }
} // namespace

void test_parked_only_when_all_true(void)
{
    for (uint8_t fitted = 0; fitted <= 2; ++fitted)
    {
        const ParkResult r = evaluatePark(parkedInputs(fitted));
        TEST_ASSERT_TRUE(r.parked);
        TEST_ASSERT_EQUAL_UINT32(0, r.reasons);
        TEST_ASSERT_FALSE(r.disagree);
        TEST_ASSERT_EQUAL_STRING("parked", describe(r).c_str());
    }
}

void test_unfitted_switches_are_ignored(void)
{
    ParkInputs in = parkedInputs(0);
    in.switch1 = SwitchReading::Unreadable;
    in.switch2 = SwitchReading::Open;
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
    in = parkedInputs(1);
    in.switch2 = SwitchReading::Unreadable;
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
}

void test_not_parked_when_nina_not_configured(void)
{
    ParkInputs in = parkedInputs(0);
    in.nina = SQM::Nina::failedReading(ReadingStatus::NotConfigured, "", NOW_MS);
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkNinaNotConfigured, r.reasons);
    TEST_ASSERT_EQUAL_STRING("not parked: N.I.N.A. not configured", describe(r).c_str());
}

void test_not_parked_for_each_nina_failure(void)
{
    struct Case
    {
        ReadingStatus status;
        uint32_t reason;
        const char *text;
    };
    const Case cases[] = {
        {ReadingStatus::Waiting, ParkNinaWaiting, "not parked: N.I.N.A. not read yet"},
        {ReadingStatus::Unreachable, ParkNinaUnreachable, "not parked: N.I.N.A. not responding: boom"},
        {ReadingStatus::Unreadable, ParkNinaUnreadable, "not parked: N.I.N.A. reply unreadable: boom"},
        {ReadingStatus::ApiError, ParkNinaApiError, "not parked: N.I.N.A. error: boom"},
    };
    for (const Case &c : cases)
    {
        ParkInputs in = parkedInputs(0);
        in.nina = SQM::Nina::failedReading(c.status, c.status == ReadingStatus::Waiting ? "" : "boom", NOW_MS);
        const ParkResult r = evaluatePark(in);
        TEST_ASSERT_FALSE(r.parked);
        TEST_ASSERT_EQUAL_UINT32(c.reason, r.reasons);
        TEST_ASSERT_EQUAL_STRING(c.text, describe(r).c_str());
    }
}

void test_nina_fresh_at_exactly_the_limit(void)
{
    ParkInputs in = parkedInputs(0);
    in.nina.takenAtMs = NOW_MS - NINA_FRESH_MS;
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
}

void test_not_parked_when_nina_stale_by_one_ms(void)
{
    ParkInputs in = parkedInputs(2);
    in.nina.takenAtMs = NOW_MS - NINA_FRESH_MS - 1;
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkNinaStale, r.reasons);
    TEST_ASSERT_FALSE(r.disagree); // a stale reading can't disagree
    TEST_ASSERT_EQUAL_STRING("not parked: N.I.N.A. stale: reading 10.0 s old > 10 s", describe(r).c_str());
}

// A reading older than a 32-bit millis() can count stays stale (convergence F1).
void test_not_parked_after_very_long_silence(void)
{
    for (uint64_t silence : {(uint64_t{1} << 31) + 1, (uint64_t{1} << 32) - 1, (uint64_t{1} << 32) + 1000})
    {
        ParkInputs in = parkedInputs(0);
        in.nina.takenAtMs = 5000;
        in.nowMs = 5000 + silence;
        const ParkResult r = evaluatePark(in);
        TEST_ASSERT_FALSE(r.parked);
        TEST_ASSERT_EQUAL_UINT32(ParkNinaStale, r.reasons);
    }
}

void test_not_parked_when_mount_not_connected(void)
{
    ParkInputs in = parkedInputs(1);
    in.nina = mount(false, false, false);
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkMountNotConnected, r.reasons);
    TEST_ASSERT_FALSE(r.disagree);
}

void test_not_parked_when_slewing_even_at_park(void)
{
    ParkInputs in = parkedInputs(0);
    in.nina = mount(true, true, true);
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkMountSlewing, r.reasons);
}

void test_not_parked_when_not_at_park(void)
{
    ParkInputs in = parkedInputs(0);
    in.nina = mount(true, false, false);
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkMountNotAtPark, r.reasons);
    TEST_ASSERT_EQUAL_STRING("not parked: N.I.N.A. says mount not at park", describe(r).c_str());
}

void test_not_parked_when_switch_unreadable(void)
{
    ParkInputs in = parkedInputs(2);
    in.switch2 = SwitchReading::Unreadable;
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkSwitch2Unreadable, r.reasons);
    TEST_ASSERT_FALSE(r.disagree); // unreadable isn't "open"
}

void test_disagree_nina_parked_switch_open(void)
{
    ParkInputs in = parkedInputs(2);
    in.switch1 = SwitchReading::Open;
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_TRUE(r.disagree);
    TEST_ASSERT_EQUAL_UINT32(ParkSwitch1Open | ParkSourcesDisagree, r.reasons);
    TEST_ASSERT_EQUAL_STRING("not parked: Park switch 1 open; N.I.N.A. and the park switches disagree", describe(r).c_str());
}

void test_disagree_nina_unparked_switches_closed(void)
{
    ParkInputs in = parkedInputs(1);
    in.nina = mount(true, false, false);
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_TRUE(r.disagree);
    TEST_ASSERT_EQUAL_UINT32(ParkMountNotAtPark | ParkSourcesDisagree, r.reasons);
}

void test_agree_not_parked_is_not_disagreement(void)
{
    ParkInputs in = parkedInputs(2);
    in.nina = mount(true, false, false);
    in.switch1 = SwitchReading::Open;
    in.switch2 = SwitchReading::Open;
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_FALSE(r.disagree);
}

void test_nina_unreachable_with_switches_closed_is_not_parked(void)
{
    ParkInputs in = parkedInputs(2);
    in.nina = SQM::Nina::failedReading(ReadingStatus::Unreachable, "timeout", NOW_MS);
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_FALSE(r.disagree);
}

namespace
{
    constexpr int MOUNT_CASES = 6; // not configured, unreachable, stale, disconnected, connected (+ atPark/slewing below)
    const SwitchReading SWITCH_STATES[] = {SwitchReading::Closed, SwitchReading::Open, SwitchReading::Unreadable};

    MountReading sweepMount(int kind, bool atPark, bool slewing)
    {
        switch (kind)
        {
        case 0:
            return SQM::Nina::failedReading(ReadingStatus::NotConfigured, "", NOW_MS);
        case 1:
            return SQM::Nina::failedReading(ReadingStatus::Unreachable, "timeout", NOW_MS);
        case 2:
            return mount(true, atPark, slewing, NOW_MS - NINA_FRESH_MS - 1);
        case 3:
            return mount(false, atPark, slewing);
        default:
            return mount(true, atPark, slewing);
        }
    }

    bool allFittedClosed(uint8_t fitted, SwitchReading s1, SwitchReading s2)
    {
        return (fitted < 1 || s1 == SwitchReading::Closed) && (fitted < 2 || s2 == SwitchReading::Closed);
    }
} // namespace

// Parked implies every condition of constitution II, over every combination.
void test_parked_implies_every_condition_exhaustive(void)
{
    for (int kind = 0; kind < MOUNT_CASES; ++kind)
        for (int flags = 0; flags < 4; ++flags)
            for (uint8_t fitted = 0; fitted <= 2; ++fitted)
                for (SwitchReading s1 : SWITCH_STATES)
                    for (SwitchReading s2 : SWITCH_STATES)
                    {
                        ParkInputs in;
                        in.nina = sweepMount(kind, flags & 1, flags & 2);
                        in.fittedSwitches = fitted;
                        in.switch1 = s1;
                        in.switch2 = s2;
                        in.nowMs = NOW_MS;
                        const bool ninaParked = kind >= 4 && (flags & 1) && !(flags & 2);
                        const ParkResult r = evaluatePark(in);
                        TEST_ASSERT_EQUAL_MESSAGE(ninaParked && allFittedClosed(fitted, s1, s2), r.parked, describe(r).c_str());
                        TEST_ASSERT_EQUAL(r.parked, r.reasons == 0);
                        if (r.disagree)
                            TEST_ASSERT_FALSE(r.parked);
                    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parked_only_when_all_true);
    RUN_TEST(test_unfitted_switches_are_ignored);
    RUN_TEST(test_not_parked_when_nina_not_configured);
    RUN_TEST(test_not_parked_for_each_nina_failure);
    RUN_TEST(test_nina_fresh_at_exactly_the_limit);
    RUN_TEST(test_not_parked_when_nina_stale_by_one_ms);
    RUN_TEST(test_not_parked_after_very_long_silence);
    RUN_TEST(test_not_parked_when_mount_not_connected);
    RUN_TEST(test_not_parked_when_slewing_even_at_park);
    RUN_TEST(test_not_parked_when_not_at_park);
    RUN_TEST(test_not_parked_when_switch_unreadable);
    RUN_TEST(test_disagree_nina_parked_switch_open);
    RUN_TEST(test_disagree_nina_unparked_switches_closed);
    RUN_TEST(test_agree_not_parked_is_not_disagreement);
    RUN_TEST(test_nina_unreachable_with_switches_closed_is_not_parked);
    RUN_TEST(test_parked_implies_every_condition_exhaustive);
    return UNITY_END();
}
