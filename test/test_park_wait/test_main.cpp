// Spec 027: an Open or Close waiting for the park (constitution 7.0.0, II).

#include <unity.h>

#include "ParkWait.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    ParkWaitSettings settings()
    {
        ParkWaitSettings s;
        s.limitMs = 240000;
        s.gapMs = 30000;
        s.holdMs = 4000;
        return s;
    }

    ParkWait waitingClose(uint32_t nowMs = 1000)
    {
        ParkWait wait;
        wait.start(Roof::Command::Close, Roof::Origin::Client, 0, nowMs, settings());
        return wait;
    }
} // namespace

void test_asks_at_once_and_again_after_the_gap(void)
{
    ParkWait wait = waitingClose();
    TEST_ASSERT_TRUE(wait.step(false, 1000).requestPark);
    TEST_ASSERT_TRUE(wait.asked());
    TEST_ASSERT_FALSE(wait.step(false, 30999).requestPark);
    TEST_ASSERT_TRUE(wait.step(false, 31000).requestPark);
    TEST_ASSERT_FALSE(wait.step(false, 31100).requestPark);
}

void test_runs_once_the_park_has_held_for_the_hold_time(void)
{
    ParkWait wait = waitingClose();
    wait.step(false, 1000);
    TEST_ASSERT_EQUAL(ParkWaitEnd::None, wait.step(true, 2000).end);
    TEST_ASSERT_EQUAL(ParkWaitEnd::None, wait.step(true, 5999).end);
    const ParkWaitStep ran = wait.step(true, 6000);
    TEST_ASSERT_EQUAL(ParkWaitEnd::Ran, ran.end);
    TEST_ASSERT_FALSE(ran.requestPark);
    TEST_ASSERT_FALSE(wait.waiting());
    TEST_ASSERT_FALSE(wait.failed());
}

void test_a_break_in_the_park_starts_the_hold_again(void)
{
    ParkWait wait = waitingClose();
    wait.step(true, 1000);
    wait.step(false, 4000);
    TEST_ASSERT_EQUAL(ParkWaitEnd::None, wait.step(true, 5000).end);
    TEST_ASSERT_EQUAL(ParkWaitEnd::None, wait.step(true, 8999).end);
    TEST_ASSERT_EQUAL(ParkWaitEnd::Ran, wait.step(true, 9000).end);
}

void test_gives_up_at_the_limit_and_says_so(void)
{
    ParkWait wait = waitingClose();
    TEST_ASSERT_EQUAL(ParkWaitEnd::None, wait.step(false, 240999).end);
    TEST_ASSERT_EQUAL_UINT32(1, wait.leftMs(240999));
    TEST_ASSERT_EQUAL(ParkWaitEnd::GaveUp, wait.step(false, 241000).end);
    TEST_ASSERT_FALSE(wait.waiting());
    TEST_ASSERT_TRUE(wait.failed());
    TEST_ASSERT_EQUAL_STRING("The mount didn't park within 240 s: the Close wasn't run", wait.endText().c_str());
    TEST_ASSERT_EQUAL(ParkWaitEnd::None, wait.step(true, 300000).end); // ended: nothing more
}

void test_a_park_held_at_the_limit_still_runs(void)
{
    ParkWait wait = waitingClose();
    wait.step(true, 237000);
    TEST_ASSERT_EQUAL(ParkWaitEnd::Ran, wait.step(true, 241000).end);
}

void test_cancel_ends_without_a_failure_and_clears_the_last_one(void)
{
    ParkWait wait = waitingClose();
    wait.step(false, 241000);
    TEST_ASSERT_TRUE(wait.failed());
    wait.cancel("a new command");
    TEST_ASSERT_FALSE(wait.failed());
    wait.start(Roof::Command::Open, Roof::Origin::Person, 4, 0, settings());
    wait.cancel("Stop");
    TEST_ASSERT_FALSE(wait.waiting());
    TEST_ASSERT_FALSE(wait.failed());
    TEST_ASSERT_EQUAL(ParkWaitEnd::Cancelled, wait.lastEnd());
    TEST_ASSERT_EQUAL_STRING("Stop", wait.endText().c_str());
}

void test_refuse_ends_with_a_failure(void)
{
    ParkWait wait = waitingClose();
    wait.refuse("Weather not safe: rain");
    TEST_ASSERT_FALSE(wait.waiting());
    TEST_ASSERT_TRUE(wait.failed());
    TEST_ASSERT_EQUAL(ParkWaitEnd::Refused, wait.lastEnd());
}

void test_keeps_the_command_and_overrides(void)
{
    ParkWait wait;
    wait.start(Roof::Command::Open, Roof::Origin::Person, 6, 0, settings());
    TEST_ASSERT_EQUAL(Roof::Command::Open, wait.command());
    TEST_ASSERT_EQUAL(Roof::Origin::Person, wait.origin());
    TEST_ASSERT_EQUAL_UINT8(6, wait.overrides());
}

void test_survives_the_millis_wrap(void)
{
    ParkWait wait = waitingClose(0xFFFFF000u);
    wait.step(true, 0xFFFFF000u);
    TEST_ASSERT_EQUAL(ParkWaitEnd::Ran, wait.step(true, 0xFFFFF000u + 4000u).end);
}

void test_why_a_wait_is_impossible(void)
{
    TEST_ASSERT_EQUAL_STRING("", parkWaitImpossible(true, true).c_str());
    TEST_ASSERT_TRUE(parkWaitImpossible(false, true).find("no N.I.N.A. address") != std::string::npos);
    TEST_ASSERT_TRUE(parkWaitImpossible(true, false).find("asking N.I.N.A. to park is off") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_asks_at_once_and_again_after_the_gap);
    RUN_TEST(test_runs_once_the_park_has_held_for_the_hold_time);
    RUN_TEST(test_a_break_in_the_park_starts_the_hold_again);
    RUN_TEST(test_gives_up_at_the_limit_and_says_so);
    RUN_TEST(test_a_park_held_at_the_limit_still_runs);
    RUN_TEST(test_cancel_ends_without_a_failure_and_clears_the_last_one);
    RUN_TEST(test_refuse_ends_with_a_failure);
    RUN_TEST(test_keeps_the_command_and_overrides);
    RUN_TEST(test_survives_the_millis_wrap);
    RUN_TEST(test_why_a_wait_is_impossible);
    return UNITY_END();
}
