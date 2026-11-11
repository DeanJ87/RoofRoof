// The close-on-unsafe sequence (spec 005; constitution II and III): starting, the park and failed closes (US1-US3).
// Every transition of data-model.md and every time limit at one below and at
// its boundary.

#include "../close_rig/CloseRig.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

// --- US1: rain closes the roof onto a parked mount ---

void test_unsafe_with_the_roof_open_starts_and_requests_a_park()
{
    Rig r;
    const Decision d = r.startUnsafe();
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    TEST_ASSERT_FALSE(d.alert);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_nothing_while_safe_or_closed()
{
    Rig r;
    Counts c = r.runTo(T0 + 600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    r.in.weatherSafe = false;
    r.in.roof = State::Closed;
    r.in.parked = true;
    c = r.runTo(T0 + 1200000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

void test_close_only_after_the_park_held_for_four_seconds()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 1000).action);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 1000 + PARK_SETTLE_MS - 1).action);
    const Decision d = r.at(T0 + 1000 + PARK_SETTLE_MS);
    TEST_ASSERT_EQUAL(Action::Close, d.action);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
}

void test_a_park_held_before_the_start_counts_from_when_it_was_first_seen()
{
    Rig r;
    r.in.parked = true; // parked from T0 - 50, while it was safe
    r.at(T0 - 50);
    const Decision d = r.startUnsafe();
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 - 50 + PARK_SETTLE_MS - 1).action);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 - 50 + PARK_SETTLE_MS).action);
}

void test_close_reaching_closed_ends_the_sequence()
{
    Rig r;
    r.startAndClose();
    Counts c = r.runTo(T0 + 30000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    r.in.roof = State::Closed;
    const Decision d = r.at(T0 + 30100);
    assertEvent(d, Event::Closed, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
}

void test_a_running_close_is_watched_without_a_park_request()
{
    Rig r;
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    const Decision d = r.startUnsafe();
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    r.in.roof = State::Closed;
    assertEvent(r.at(T0 + 20000), Event::Closed, Cause::None);
}

void test_a_waiting_close_is_watched_without_a_park_request()
{
    Rig r;
    r.in.closePending = true;
    const Decision d = r.startUnsafe();
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
}

void test_an_opening_or_stopped_roof_starts_the_sequence()
{
    for (State state : {State::Opening, State::Stopped})
    {
        Rig r;
        r.in.roof = state;
        TEST_ASSERT_EQUAL(Action::RequestPark, r.startUnsafe().action);
    }
}

void test_a_persons_close_while_waiting_is_watched()
{
    Rig r;
    r.startUnsafe();
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    const Decision d = r.at(T0 + 5000);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    r.in.roof = State::Closed;
    assertEvent(r.at(T0 + 30000), Event::Closed, Cause::None);
}

// --- US2: a park that can't be confirmed ---

void test_park_timeout_at_its_boundary()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    Counts c = r.runTo(T0 + 60000 - 1, 1);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    const Decision d = r.at(T0 + 60000);
    assertEvent(d, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL_UINT64(60000, d.limitMs);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
}

void test_default_park_timeout_is_240_s()
{
    Rig r;
    r.startUnsafe();
    TEST_ASSERT_EQUAL(0u, r.runTo(T0 + DEFAULT_PARK_TIMEOUT_MS - 1, 1000).events.size());
    assertEvent(r.at(T0 + DEFAULT_PARK_TIMEOUT_MS), Event::ParkNotConfirmed, Cause::ParkTimeout);
}

void test_park_timeout_setting_applies_from_the_next_attempt()
{
    Rig r;
    r.startUnsafe();
    r.seq.setParkTimeoutMs(30000);
    TEST_ASSERT_EQUAL(0u, r.runTo(T0 + 60000, 1000).events.size());
    assertEvent(r.at(T0 + DEFAULT_PARK_TIMEOUT_MS), Event::ParkNotConfirmed, Cause::ParkTimeout);
    const uint64_t retry = T0 + DEFAULT_PARK_TIMEOUT_MS + RETRY_AFTER_MS;
    TEST_ASSERT_EQUAL(Event::Started, r.at(retry).event);
    TEST_ASSERT_EQUAL(0u, r.runTo(retry + 30000 - 1, 1000).events.size());
    assertEvent(r.at(retry + 30000), Event::ParkNotConfirmed, Cause::ParkTimeout);
}

void test_nina_not_configured_fails_at_once()
{
    Rig r;
    r.in.ninaConfigured = false;
    r.startUnsafe();
    const Decision d = r.at(T0 + 10);
    assertEvent(d, Event::ParkNotConfirmed, Cause::NinaNotConfigured);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
}

void test_retry_waits_five_minutes_without_a_request()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    r.at(T0 + 60000); // failed
    const uint64_t failed = T0 + 60000;
    Counts c = r.runTo(failed + RETRY_AFTER_MS - 1, 1);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    const Decision d = r.at(failed + RETRY_AFTER_MS);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_retry_wait_closes_when_the_park_is_confirmed()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    r.at(T0 + 60000);
    r.in.parked = true;
    r.at(T0 + 70000);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 70000 + PARK_SETTLE_MS - 1).action);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 70000 + PARK_SETTLE_MS).action);
}

void test_park_confirmed_then_lost_restarts_the_count()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 1000);
    r.at(T0 + 3000);
    r.in.parked = false;
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 3100).action);
    r.in.parked = true;
    r.at(T0 + 3200);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 5100).action); // 4 s after the first confirmation
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 3200 + PARK_SETTLE_MS - 1).action);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 3200 + PARK_SETTLE_MS).action);
}

void test_gate_refusal_goes_back_to_waiting_within_the_same_limit()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
    TEST_ASSERT_EQUAL(Event::None, r.seq.closeAnswered(CloseAnswer::ParkRefused).event);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    // The park must hold for the full time again.
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 4200).action);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 4200 + PARK_SETTLE_MS - 1).action);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4200 + PARK_SETTLE_MS).action);
    r.seq.closeAnswered(CloseAnswer::ParkRefused);
    r.in.parked = false;
    TEST_ASSERT_EQUAL(0u, r.runTo(T0 + 60000 - 1, 100).events.size());
    assertEvent(r.at(T0 + 60000), Event::ParkNotConfirmed, Cause::ParkTimeout);
}

void test_gate_refusal_from_the_retry_wait_returns_to_it()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    r.at(T0 + 60000); // failed: back-off until T0 + 360000
    r.in.parked = true;
    r.at(T0 + 100000);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 104000).action);
    r.seq.closeAnswered(CloseAnswer::ParkRefused);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    r.in.parked = false;
    Counts c = r.runTo(T0 + 60000 + RETRY_AFTER_MS - 1, 1000);
    TEST_ASSERT_EQUAL(0, c.requests);
    TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + 60000 + RETRY_AFTER_MS).action);
}

void test_park_requests_repeat_every_thirty_seconds_while_waiting()
{
    Rig r;
    r.startUnsafe();
    TEST_ASSERT_EQUAL(0, r.runTo(T0 + PARK_REQUEST_GAP_MS - 1, 1).requests);
    TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + PARK_REQUEST_GAP_MS).action);
    TEST_ASSERT_EQUAL(0, r.runTo(T0 + 2 * PARK_REQUEST_GAP_MS - 1, 1).requests);
    TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + 2 * PARK_REQUEST_GAP_MS).action);
}

// --- US3: a close that fails ---

void test_fault_while_closing_alerts_and_waits_for_a_person()
{
    Rig r;
    r.startAndClose();
    r.in.roof = State::Error;
    const Decision d = r.at(T0 + 20000);
    assertEvent(d, Event::CloseFailed, Cause::Fault);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase());
    // Nothing moves; the alert is repeated every 30 min while the roof stays open.
    Counts c = r.runTo(T0 + 20000 + REMIND_MS - 1000, 1000);
    assertOnlyReminders(c, Event::CloseFailed, 0);
    c = r.runTo(T0 + 20000 + 2 * REMIND_MS, 1000);
    assertOnlyReminders(c, Event::CloseFailed, 2);
}

void test_refused_close_alerts_and_backs_off()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
    const Decision d = r.seq.closeAnswered(CloseAnswer::Refused);
    assertEvent(d, Event::CloseFailed, Cause::Refused);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    // Still parked, but no close (and no request) until the back-off ends: a
    // lasting refusal can't become a close every step.
    Counts c = r.runTo(T0 + 4100 + RETRY_AFTER_MS - 1, 100);
    TEST_ASSERT_EQUAL(0, c.closes + c.requests);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100 + RETRY_AFTER_MS).action); // the park has held throughout
}

void test_a_parked_mount_held_back_by_the_back_off_is_not_a_park_failure()
{
    Rig r;
    r.seq.setParkTimeoutMs(30000);
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
    r.seq.closeAnswered(CloseAnswer::Refused); // back-off until T0 + 4100 + 5 min
    r.in.weatherSafe = true;                   // a brief safe spell...
    r.at(T0 + 5000);
    r.in.weatherSafe = false; // ...then a new attempt while the mount stays parked
    TEST_ASSERT_EQUAL(Event::Started, r.at(T0 + 6000).event);
    Counts c = r.runTo(T0 + 4100 + RETRY_AFTER_MS - 100, 100);
    TEST_ASSERT_EQUAL(0, c.closes);
    TEST_ASSERT_EQUAL(0u, c.events.size()); // no "mount not parked" though the park limit passed
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100 + RETRY_AFTER_MS).action);
}

void test_withdrawn_close_returns_to_waiting_and_the_stop_stands_down()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
    TEST_ASSERT_EQUAL(Event::None, r.seq.closeAnswered(CloseAnswer::Withdrawn).event);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    r.in.personStops = 1; // the stop the roof saw before the close
    assertEvent(r.at(T0 + 4200), Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_close_refused_for_a_restart_goes_back_without_back_off()
{
    // Spec 010: a committed restart refuses the close ("restarting"); that is
    // neither a failed close nor a person's stop.
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
    const Decision d = r.seq.closeAnswered(answerFor(false, "restarting"));
    TEST_ASSERT_EQUAL(Event::None, d.event);
    TEST_ASSERT_FALSE(d.alert);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    TEST_ASSERT_EQUAL(Event::None, r.at(T0 + 4200).event); // no stand-down
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 8300).action); // the park held 4 s again
}

void test_platform_state_of_each_phase()
{
    // Spec 010: only waiting for the park and closing are "working" (nothing
    // may interrupt them); a back-off and the stand-downs leave the way out open.
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::Idle, platformState(Phase::Idle));
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::Working, platformState(Phase::WaitPark));
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::Working, platformState(Phase::Closing));
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::Waiting, platformState(Phase::RetryWait));
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::Waiting, platformState(Phase::CannotMove));
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::Waiting, platformState(Phase::Halted));
    TEST_ASSERT_EQUAL(RoofIo::SequenceState::HeldByPerson, platformState(Phase::HeldByPerson));
}

void test_answer_for_the_roofs_outcome()
{
    TEST_ASSERT_EQUAL(CloseAnswer::Started, answerFor(true, ""));
    TEST_ASSERT_EQUAL(CloseAnswer::ParkRefused, answerFor(false, RoofIo::gateRefusalName(RoofIo::GateRefusal::MountNotParked)));
    TEST_ASSERT_EQUAL(CloseAnswer::ParkRefused, answerFor(false, "mountNotParked"));
    // A restart about to happen (spec 010): not a failed close, no back-off.
    TEST_ASSERT_EQUAL(CloseAnswer::Withdrawn, answerFor(false, "restarting"));
    TEST_ASSERT_EQUAL(CloseAnswer::Refused, answerFor(false, "busy"));
    TEST_ASSERT_EQUAL(CloseAnswer::Withdrawn, answerFor(false, PERSON_STOP_REASON));
    TEST_ASSERT_EQUAL(CloseAnswer::Refused, answerFor(false, "supplyLow"));
    TEST_ASSERT_EQUAL(CloseAnswer::Refused, answerFor(false, "notStarted"));
    TEST_ASSERT_EQUAL(CloseAnswer::Refused, answerFor(false, "weatherNotSafe"));
}

void test_answers_outside_a_close_are_ignored()
{
    Rig r;
    TEST_ASSERT_EQUAL(Event::None, r.seq.closeAnswered(CloseAnswer::Refused).event);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.startUnsafe();
    r.seq.closeAnswered(CloseAnswer::ParkRefused);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_close_started_answer_keeps_closing()
{
    Rig r;
    r.startAndClose();
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
}

void test_waiting_close_guard_at_its_boundary()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    r.at(T0 + 4100); // close commanded
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.closePending = true;
    Counts c = r.runTo(T0 + 4100 + CLOSE_START_MS - 1, 1);
    TEST_ASSERT_EQUAL(0, c.stops);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    const Decision d = r.at(T0 + 4100 + CLOSE_START_MS);
    TEST_ASSERT_EQUAL(Action::Stop, d.action);
    assertEvent(d, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_EQUAL_UINT64(CLOSE_START_MS, d.limitMs);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
}

void test_moving_close_guard_at_its_boundary()
{
    Rig r;
    r.startAndClose(); // close commanded at T0 + 4100; first seen closing only after a late step
    r.at(T0 + 54100);
    Counts c = r.runTo(T0 + 4100 + 60000 + CLOSE_GUARD_SLACK_MS - 1, 1);
    TEST_ASSERT_EQUAL(0, c.stops);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    const Decision d = r.at(T0 + 4100 + 60000 + CLOSE_GUARD_SLACK_MS);
    TEST_ASSERT_EQUAL(Action::Stop, d.action);
    assertEvent(d, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_EQUAL_UINT64(60000 + CLOSE_GUARD_SLACK_MS, d.limitMs);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
}

void test_a_halted_close_stays_stopped_where_it_is()
{
    Rig r;
    r.startAndClose();
    const uint64_t tripped = T0 + 4100 + 60000 + CLOSE_GUARD_SLACK_MS;
    TEST_ASSERT_EQUAL(Action::Stop, r.at(tripped).action);
    r.in.roof = State::Stopped; // the stop kept the estimate: the logic would accept a close
    r.in.moveTimeoutMs = 0;
    Counts c = r.runTo(tripped + REMIND_MS - 1000, 1000); // parked, unsafe
    assertOnlyReminders(c, Event::CloseFailed, 0);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
    c = r.runTo(tripped + 3 * 3600000ULL, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    // Only a closed roof or a long safe spell ends it.
    r.in.roof = State::Closed;
    assertEvent(r.at(tripped + 3 * 3600000ULL + 1000), Event::Closed, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

void test_close_guard_after_a_waiting_close_counts_from_its_start()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    r.at(T0 + 4100); // close commanded, waiting
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.closePending = true;
    r.at(T0 + 10000);
    r.in.closePending = false;
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    r.at(T0 + 20000); // started
    TEST_ASSERT_EQUAL(0, r.runTo(T0 + 20000 + 90000 - 1, 1).stops);
    TEST_ASSERT_EQUAL(Action::Stop, r.at(T0 + 20000 + 90000).action);
}

void test_interrupted_close_alerts_and_backs_off()
{
    Rig r;
    r.startAndClose();
    r.at(T0 + 5000);
    r.in.roof = State::Stopped;
    const Decision d = r.at(T0 + 6000);
    assertEvent(d, Event::CloseFailed, Cause::Interrupted);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    // No re-drive straight away, though the park still holds.
    Counts c = r.runTo(T0 + 6000 + RETRY_AFTER_MS - 1, 100);
    TEST_ASSERT_EQUAL(0, c.closes + c.requests + c.stops);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 6000 + RETRY_AFTER_MS).action); // the park has held throughout
}

void test_close_ending_part_way_after_the_weather_cleared_is_quiet()
{
    Rig r;
    r.startAndClose();
    r.in.weatherSafe = true;
    r.in.roof = State::Opening; // a person opened it once it was safe
    const Decision d = r.at(T0 + 6000);
    assertEvent(d, Event::EndedSafe, Cause::None);
    TEST_ASSERT_FALSE(d.alert);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

void test_each_critical_event_alerts_at_most_every_thirty_minutes()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    TEST_ASSERT_TRUE(r.at(T0 + 60000).alert);
    const uint64_t second = T0 + 60000 + RETRY_AFTER_MS;
    r.at(second);
    const Decision repeat = r.at(second + 60000);
    assertEvent(repeat, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_FALSE(repeat.alert);
    // A new spell after a long safe one, still within 30 minutes: logged, not alerted.
    r.in.weatherSafe = true;
    r.runTo(second + 61000 + REARM_SAFE_MS + 60000, 1000);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.weatherSafe = false;
    Counts c = r.runTo(T0 + 60000 + REMIND_MS - 1, 1000);
    TEST_ASSERT_EQUAL(0, c.alerts);
    TEST_ASSERT_TRUE(c.events.size() >= 2); // started, parkNotConfirmed
    // 30 minutes after the last alert, the next failure alerts again.
    c = r.runTo(T0 + 60000 + REMIND_MS + 400000, 1000);
    TEST_ASSERT_TRUE(c.alerts >= 1);
}

void test_a_new_cause_alerts_though_its_event_alerted_recently()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
    TEST_ASSERT_TRUE(r.seq.closeAnswered(CloseAnswer::Refused).alert); // supply low, say
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100 + RETRY_AFTER_MS).action);
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    r.at(T0 + 4200 + RETRY_AFTER_MS);
    r.in.roof = State::Error; // then the close faults
    const Decision d = r.at(T0 + 10000 + RETRY_AFTER_MS);
    assertEvent(d, Event::CloseFailed, Cause::Fault);
    TEST_ASSERT_TRUE(d.alert);
}

void test_repeated_gate_refusals_count_as_a_refusal()
{
    Rig r;
    r.startUnsafe();
    r.in.parked = true;
    r.at(T0 + 100);
    uint64_t t = T0 + 4100;
    for (uint8_t i = 1; i < MAX_PARK_REFUSALS; ++i)
    {
        TEST_ASSERT_EQUAL(Action::Close, r.at(t).action);
        TEST_ASSERT_EQUAL(Event::None, r.seq.closeAnswered(CloseAnswer::ParkRefused).event);
        r.at(t + 100);
        t += 100 + PARK_SETTLE_MS;
    }
    TEST_ASSERT_EQUAL(Action::Close, r.at(t).action);
    const Decision d = r.seq.closeAnswered(CloseAnswer::ParkRefused);
    assertEvent(d, Event::CloseFailed, Cause::Refused);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    TEST_ASSERT_EQUAL(0, r.runTo(t + RETRY_AFTER_MS - 1, 100).closes);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_unsafe_with_the_roof_open_starts_and_requests_a_park);
    RUN_TEST(test_nothing_while_safe_or_closed);
    RUN_TEST(test_close_only_after_the_park_held_for_four_seconds);
    RUN_TEST(test_a_park_held_before_the_start_counts_from_when_it_was_first_seen);
    RUN_TEST(test_close_reaching_closed_ends_the_sequence);
    RUN_TEST(test_a_running_close_is_watched_without_a_park_request);
    RUN_TEST(test_a_waiting_close_is_watched_without_a_park_request);
    RUN_TEST(test_an_opening_or_stopped_roof_starts_the_sequence);
    RUN_TEST(test_a_persons_close_while_waiting_is_watched);
    RUN_TEST(test_park_timeout_at_its_boundary);
    RUN_TEST(test_default_park_timeout_is_240_s);
    RUN_TEST(test_park_timeout_setting_applies_from_the_next_attempt);
    RUN_TEST(test_nina_not_configured_fails_at_once);
    RUN_TEST(test_retry_waits_five_minutes_without_a_request);
    RUN_TEST(test_retry_wait_closes_when_the_park_is_confirmed);
    RUN_TEST(test_park_confirmed_then_lost_restarts_the_count);
    RUN_TEST(test_gate_refusal_goes_back_to_waiting_within_the_same_limit);
    RUN_TEST(test_gate_refusal_from_the_retry_wait_returns_to_it);
    RUN_TEST(test_park_requests_repeat_every_thirty_seconds_while_waiting);
    RUN_TEST(test_fault_while_closing_alerts_and_waits_for_a_person);
    RUN_TEST(test_refused_close_alerts_and_backs_off);
    RUN_TEST(test_a_parked_mount_held_back_by_the_back_off_is_not_a_park_failure);
    RUN_TEST(test_withdrawn_close_returns_to_waiting_and_the_stop_stands_down);
    RUN_TEST(test_close_refused_for_a_restart_goes_back_without_back_off);
    RUN_TEST(test_platform_state_of_each_phase);
    RUN_TEST(test_answer_for_the_roofs_outcome);
    RUN_TEST(test_answers_outside_a_close_are_ignored);
    RUN_TEST(test_close_started_answer_keeps_closing);
    RUN_TEST(test_waiting_close_guard_at_its_boundary);
    RUN_TEST(test_moving_close_guard_at_its_boundary);
    RUN_TEST(test_a_halted_close_stays_stopped_where_it_is);
    RUN_TEST(test_close_guard_after_a_waiting_close_counts_from_its_start);
    RUN_TEST(test_interrupted_close_alerts_and_backs_off);
    RUN_TEST(test_close_ending_part_way_after_the_weather_cleared_is_quiet);
    RUN_TEST(test_each_critical_event_alerts_at_most_every_thirty_minutes);
    RUN_TEST(test_a_new_cause_alerts_though_its_event_alerted_recently);
    RUN_TEST(test_repeated_gate_refusals_count_as_a_refusal);
    return UNITY_END();
}
