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

// Owner decision 2026-11-24: an Open that starts the roof moving (a person's
// or a client's) ends the stand-down at once, as the re-arm spell does: rain
// soon after closes the roof again. A refused Open (the roof stays put) doesn't.
void test_an_open_that_starts_moving_ends_the_stand_down()
{
    Rig r;
    heldAt(r);
    r.in.roof = State::Stopped;
    r.in.weatherSafe = true;
    r.at(T0 + 2000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.in.roof = State::Opening;
    const Decision d = r.at(T0 + 3000);
    assertEvent(d, Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Action::None, d.action); // nothing more moves for it
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    TEST_ASSERT_TRUE(r.seq.standDownLeftMs() == NOT_COUNTING);
    TEST_ASSERT_TRUE(openEndedHold(Phase::HeldByPerson, Phase::Idle, State::Opening));
    TEST_ASSERT_FALSE(openEndedHold(Phase::HeldByPerson, Phase::Idle, State::Open)); // the re-arm spell, say
    TEST_ASSERT_FALSE(openEndedHold(Phase::CannotMove, Phase::Idle, State::Opening));
    r.in.roof = State::Open;
    r.at(T0 + 60000);
    // Rain within the re-arm spell: a new attempt at once.
    r.in.weatherSafe = false;
    const Decision again = r.at(T0 + 61000);
    assertEvent(again, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, again.action);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_a_refused_open_keeps_the_stand_down()
{
    Rig r;
    heldAt(r);
    r.in.roof = State::Stopped; // a refused Open leaves the roof where it stood
    r.in.weatherSafe = true;
    const Counts c = r.runTo(T0 + 60000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.in.weatherSafe = false;
    r.runTo(T0 + 120000, 1000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_an_open_ends_neither_a_halted_close_nor_a_cannot_move()
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
        // A close that timed out waits for a person or the re-arm spell: an
        // Open doesn't end it (its alert keeps being sent until delivered).
        r.in.roof = State::Opening;
        r.at(r.in.nowMs + 1000);
        TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
        TEST_ASSERT_FALSE(openEndedHold(Phase::Halted, Phase::Idle, State::Opening));
    }
    {
        // A person's slow open from an unknown position: the position becomes
        // known, so the back-off follows as before (not a release).
        Rig r;
        r.in.roof = State::Unknown;
        r.startUnsafe();
        r.runTo(T0 + 3000);
        TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase());
        r.in.roof = State::Opening;
        r.at(r.in.nowMs + 100);
        TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    }
}

// A Stop and an Open seen on the same step while stood down: the roof reads
// opening, so the Open came last (after it, a Stop would leave it stopped).
void test_a_stop_and_an_open_seen_on_one_step_end_the_stand_down()
{
    Rig r;
    heldAt(r);
    r.in.weatherSafe = true;
    r.at(T0 + 2000);
    r.in.personStops = 2;
    r.in.roof = State::Opening;
    assertEvent(r.at(T0 + 3000), Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

// The gate refuses an Open while unsafe; if one is seen anyway (a race with the
// verdict), ending the stand-down only arms the close: an attempt starts at once.
void test_an_open_seen_while_unsafe_arms_the_close_at_once()
{
    Rig r;
    heldAt(r);
    r.in.roof = State::Opening;
    assertEvent(r.at(T0 + 2000), Event::EndedSafe, Cause::None);
    const Decision d = r.at(T0 + 2100);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

// A Stop and then an Open between two steps: the roof reads opening, so the
// stand-down the stop began ends on the next step.
void test_a_stop_then_an_open_between_two_steps_ends_the_stand_down()
{
    Rig r;
    r.startAndClose();
    r.in.weatherSafe = true; // safe while closing: the close carries on
    r.at(T0 + 5000);
    r.in.personStops = 1;
    r.in.roof = State::Opening;
    assertEvent(r.at(T0 + 6000), Event::StoppedByPerson, Cause::None);
    assertEvent(r.at(T0 + 6100), Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
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
    RUN_TEST(test_an_open_that_starts_moving_ends_the_stand_down);
    RUN_TEST(test_a_refused_open_keeps_the_stand_down);
    RUN_TEST(test_an_open_ends_neither_a_halted_close_nor_a_cannot_move);
    RUN_TEST(test_a_stop_and_an_open_seen_on_one_step_end_the_stand_down);
    RUN_TEST(test_an_open_seen_while_unsafe_arms_the_close_at_once);
    RUN_TEST(test_a_stop_then_an_open_between_two_steps_ends_the_stand_down);
    return UNITY_END();
}
