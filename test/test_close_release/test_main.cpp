// Spec 026: a person's attended release of the automatic close's stand-down
// (only while nothing wants the roof closed; it never moves the roof) and the
// stand-down's countdown (constitution III: the close is only armed again).

#include <cstring>

#include "../close_rig/CloseRig.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

// Stood down by a person's stop at T0 + 1000, the weather unsafe.
static void heldAt(Rig &r)
{
    r.startUnsafe();
    r.in.personStops = 1;
    r.at(T0 + 1000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_release_while_safe_ends_the_stand_down_and_arms_the_close_again()
{
    Rig r;
    heldAt(r);
    r.in.weatherSafe = true;
    r.at(T0 + 2000);
    r.in.releaseHold = true;
    const Decision d = r.at(T0 + 3000);
    assertEvent(d, Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Action::None, d.action); // the release never moves the roof
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.releaseHold = false;
    r.in.weatherSafe = false; // unsafe again: a new attempt, as after the re-arm spell
    assertEvent(r.at(T0 + 4000), Event::Started, Cause::None);
    // The park request keeps its spacing from the last one (T0), then goes out.
    TEST_ASSERT_EQUAL(1, r.runTo(T0 + PARK_REQUEST_GAP_MS + 1000, 1000).requests);
}

void test_release_is_ignored_while_the_weather_is_not_safe()
{
    Rig r;
    heldAt(r);
    r.in.releaseHold = true;
    Counts c = r.runTo(T0 + 60000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_release_is_ignored_while_daylight_wants_the_roof_closed()
{
    Rig r;
    heldAt(r);
    r.in.weatherSafe = true;
    r.in.daylight = true;
    r.in.releaseHold = true;
    r.runTo(T0 + 60000, 1000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_a_new_stop_on_the_same_step_wins_over_a_release()
{
    Rig r;
    heldAt(r);
    r.in.weatherSafe = true;
    r.at(T0 + 2000);
    r.in.personStops = 2;
    r.in.releaseHold = true;
    r.at(T0 + 3000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_release_ends_a_halted_close_but_not_a_cannot_move()
{
    {
        Rig r;
        r.startAndClose();
        r.at(T0 + 5000);
        assertEvent(r.at(T0 + 4100 + 60000 + CLOSE_GUARD_SLACK_MS + 1000), Event::CloseFailed, Cause::TooLong);
        r.in.roof = State::Stopped;
        r.in.weatherSafe = true;
        r.at(r.in.nowMs + 1000);
        TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
        r.in.releaseHold = true;
        assertEvent(r.at(r.in.nowMs + 1000), Event::EndedSafe, Cause::None);
        TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    }
    {
        Rig r;
        r.in.roof = State::Error;
        r.startUnsafe();
        TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase());
        r.in.weatherSafe = true;
        r.in.releaseHold = true;
        r.runTo(T0 + 60000, 1000);
        TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase()); // a fault is cleared by a person, not released
    }
}

void test_stand_down_countdown_runs_only_while_safe()
{
    Rig r;
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == NOT_COUNTING); // idle
    heldAt(r);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == NOT_COUNTING); // unsafe: not counting
    r.in.weatherSafe = true;
    r.at(T0 + 2000); // safe from here: ends at T0 + 2000 + REARM_SAFE_MS
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == REARM_SAFE_MS);
    r.at(T0 + 62000);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == REARM_SAFE_MS - 60000);
    r.in.weatherSafe = false; // the count starts again once it is safe again
    r.at(T0 + 63000);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == NOT_COUNTING);
    r.in.weatherSafe = true;
    r.at(T0 + 64000);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == REARM_SAFE_MS);
    r.runTo(T0 + 64000 + REARM_SAFE_MS - 1000, 1000);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == 1000);
    assertEvent(r.at(T0 + 64000 + REARM_SAFE_MS), Event::EndedSafe, Cause::None);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == NOT_COUNTING);
}

void test_stand_down_countdown_counts_from_the_stop_when_safe_before_it()
{
    Rig r;
    r.startAndClose();
    r.in.weatherSafe = true; // safe while closing: the close carries on
    r.at(T0 + 5000);
    r.in.personStops = 1;
    r.in.roof = State::Stopped;
    r.at(T0 + 9000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == REARM_SAFE_MS); // from the stop, not from T0 + 5000
}

void test_release_refusal_names_why(void)
{
    TEST_ASSERT_NULL(releaseRefusal(Phase::HeldByPerson, true));
    TEST_ASSERT_NULL(releaseRefusal(Phase::Halted, true));
    TEST_ASSERT_NOT_NULL(releaseRefusal(Phase::HeldByPerson, false)); // something wants the roof closed
    TEST_ASSERT_NOT_NULL(releaseRefusal(Phase::Idle, true));
    TEST_ASSERT_NOT_NULL(releaseRefusal(Phase::CannotMove, true));
    TEST_ASSERT_NOT_NULL(releaseRefusal(Phase::WaitPark, true));
    TEST_ASSERT_EQUAL_STRING("Stand-down released by a person at the roof", releaseLogText(Phase::HeldByPerson, Phase::Idle));
    TEST_ASSERT_NOT_EQUAL(
        0, std::strcmp("Stand-down released by a person at the roof", releaseLogText(Phase::HeldByPerson, Phase::HeldByPerson)));
}

void test_no_countdown_while_daylight_wants_the_roof_closed(void)
{
    Rig r;
    heldAt(r);
    r.in.weatherSafe = true;
    r.in.daylight = true;
    r.runTo(T0 + 60000, 1000);
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == NOT_COUNTING);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_release_while_safe_ends_the_stand_down_and_arms_the_close_again);
    RUN_TEST(test_release_is_ignored_while_the_weather_is_not_safe);
    RUN_TEST(test_release_is_ignored_while_daylight_wants_the_roof_closed);
    RUN_TEST(test_a_new_stop_on_the_same_step_wins_over_a_release);
    RUN_TEST(test_release_ends_a_halted_close_but_not_a_cannot_move);
    RUN_TEST(test_stand_down_countdown_runs_only_while_safe);
    RUN_TEST(test_stand_down_countdown_counts_from_the_stop_when_safe_before_it);
    RUN_TEST(test_release_refusal_names_why);
    RUN_TEST(test_no_countdown_while_daylight_wants_the_roof_closed);
    return UNITY_END();
}
