// The close-on-unsafe sequence (spec 005; constitution II and III): a person's stop, unknown position, weather clearing, wording (US4-US6).
// Every transition of data-model.md and every time limit at one below and at
// its boundary.

#include "../close_rig/CloseRig.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

// --- US4: a person's stop is respected ---

void test_persons_stop_while_closing_stands_down()
{
    Rig r;
    r.startAndClose();
    r.in.personStops = 1;
    r.in.roof = State::Stopped;
    const Decision d = r.at(T0 + 10000);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    Counts c = r.runTo(T0 + 6 * 3600000ULL, 1000);      // six hours, parked, unsafe
    assertOnlyReminders(c, Event::StoppedByPerson, 11); // every 30 min after the first
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_persons_stop_while_waiting_or_backing_off_stands_down()
{
    {
        Rig r;
        r.startUnsafe();
        r.in.personStops = 7;
        assertEvent(r.at(T0 + 1000), Event::StoppedByPerson, Cause::None);
        r.in.parked = true;
        TEST_ASSERT_EQUAL(0, r.runTo(T0 + 3600000, 1000).closes);
    }
    {
        Rig r;
        r.seq.setParkTimeoutMs(60000);
        r.startUnsafe();
        r.at(T0 + 60000);
        TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
        r.in.personStops = 1;
        assertEvent(r.at(T0 + 61000), Event::StoppedByPerson, Cause::None);
        r.in.parked = true;
        Counts c = r.runTo(T0 + 3600000, 1000);
        TEST_ASSERT_EQUAL(0, c.requests + c.closes);
    }
}

void test_stood_down_rearms_after_ten_minutes_safe()
{
    Rig r;
    r.startUnsafe();
    r.in.personStops = 1;
    r.at(T0 + 1000);
    r.in.weatherSafe = true;
    r.at(T0 + 2000); // safe from here
    TEST_ASSERT_EQUAL(0u, r.runTo(T0 + 2000 + REARM_SAFE_MS - 1, 1000).events.size());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    assertEvent(r.at(T0 + 2000 + REARM_SAFE_MS), Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + 2000 + REARM_SAFE_MS + 1000);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
}

void test_a_broken_safe_run_does_not_rearm()
{
    Rig r;
    r.startUnsafe();
    r.in.personStops = 1;
    r.at(T0 + 1000);
    r.in.weatherSafe = true;
    r.runTo(T0 + 1000 + 9 * 60000, 1000);
    r.in.weatherSafe = false;
    r.at(T0 + 1000 + 9 * 60000 + 100);
    r.in.weatherSafe = true;
    Counts c = r.runTo(T0 + 1000 + 18 * 60000, 1000);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_persons_close_while_stood_down_ends_it()
{
    Rig r;
    r.startUnsafe();
    r.in.personStops = 1;
    r.at(T0 + 1000);
    r.in.roof = State::Closing;
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 2000).action);
    r.in.roof = State::Closed;
    assertEvent(r.at(T0 + 40000), Event::Closed, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

void test_stops_before_the_episode_are_ignored()
{
    Rig r;
    r.in.personStops = 3; // pressed while it was safe
    r.at(T0 - 50);
    r.startUnsafe();
    const Decision d = r.at(T0 + 1000);
    TEST_ASSERT_EQUAL(Event::None, d.event);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_the_count_at_start_up_is_history()
{
    Sequence seq;
    Inputs in;
    in.nowMs = 5000;
    in.weatherSafe = false;
    in.roof = State::Open;
    in.personStops = 42;
    TEST_ASSERT_EQUAL(Event::Started, seq.step(in).event);
    in.nowMs = 5100;
    TEST_ASSERT_EQUAL(Event::None, seq.step(in).event);
}

// --- US5: never move by itself from an unknown position ---

void test_unknown_position_stands_down_after_two_seconds()
{
    Rig r;
    r.in.roof = State::Unknown;
    r.at(T0 - 50); // unknown from here
    r.in.weatherSafe = false;
    Counts c = r.runTo(T0 - 50 + UNKNOWN_CONFIRM_MS - 1, 1);
    TEST_ASSERT_EQUAL(0, c.requests);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    const Decision d = r.at(T0 - 50 + UNKNOWN_CONFIRM_MS);
    assertEvent(d, Event::CannotClose, Cause::PositionUnknown);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    r.in.parked = true;
    c = r.runTo(T0 - 50 + UNKNOWN_CONFIRM_MS + REMIND_MS - 1000, 1000);
    assertOnlyReminders(c, Event::CannotClose, 0);
    c = r.runTo(T0 - 50 + UNKNOWN_CONFIRM_MS + REMIND_MS, 1);
    assertOnlyReminders(c, Event::CannotClose, 1);
}

void test_a_stop_while_unable_to_move_stands_down_once_it_can()
{
    Rig r;
    r.in.roof = State::Error;
    r.startUnsafe();
    r.in.parked = true;
    r.in.personStops = 1; // a person at the roof presses stop...
    assertEvent(r.at(T0 + 1000), Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.in.roof = State::Open; // ...then clears the fault at the open magnet
    Counts c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
}

void test_unknown_at_start_up_that_settles_starts_normally()
{
    Sequence seq;
    Inputs in;
    in.nowMs = 100;
    in.weatherSafe = false;
    in.roof = State::Unknown;
    TEST_ASSERT_EQUAL(Action::None, seq.step(in).action);
    in.nowMs = 160;
    in.roof = State::Open;
    const Decision d = seq.step(in);
    TEST_ASSERT_EQUAL(Event::Started, d.event);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
}

void test_fault_latched_cannot_close()
{
    Rig r;
    r.in.roof = State::Error;
    const Decision d = r.startUnsafe();
    assertEvent(d, Event::CannotClose, Cause::FaultLatched);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    r.in.parked = true;
    Counts c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
}

void test_fault_while_waiting_stands_down()
{
    Rig r;
    r.startUnsafe();
    r.in.roof = State::Error;
    assertEvent(r.at(T0 + 1000), Event::CannotClose, Cause::FaultLatched);
    TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase());
}

void test_unknown_while_waiting_or_closing_stands_down_after_two_seconds()
{
    Rig r;
    r.startAndClose();
    r.in.roof = State::Unknown;
    r.at(T0 + 5000);
    TEST_ASSERT_EQUAL(Event::None, r.at(T0 + 5000 + UNKNOWN_CONFIRM_MS - 1).event);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    assertEvent(r.at(T0 + 5000 + UNKNOWN_CONFIRM_MS), Event::CannotClose, Cause::PositionUnknown);
}

void test_resumes_after_a_person_makes_the_position_known()
{
    Rig r;
    r.in.roof = State::Error;
    r.startUnsafe();
    r.in.roof = State::Unknown; // a person cleared the fault between the magnets
    TEST_ASSERT_EQUAL(Phase::CannotMove, (r.at(T0 + 1000), r.seq.phase()));
    r.in.roof = State::Open; // and moved it to the open magnet
    const Decision d = r.at(T0 + 60000);
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    Counts c = r.runTo(T0 + 60000 + RETRY_AFTER_MS - 1, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes);
    TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + 60000 + RETRY_AFTER_MS).action);
    r.in.parked = true;
    r.at(T0 + 60000 + RETRY_AFTER_MS + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 60000 + RETRY_AFTER_MS + 100 + PARK_SETTLE_MS).action);
}

void test_cannot_move_ends_when_closed_or_safe_for_ten_minutes()
{
    Rig r;
    r.in.roof = State::Error;
    r.startUnsafe();
    r.in.weatherSafe = true;
    r.at(T0 + 1000);
    TEST_ASSERT_EQUAL(0u, r.runTo(T0 + 1000 + REARM_SAFE_MS - 1, 1000).events.size());
    assertEvent(r.at(T0 + 1000 + REARM_SAFE_MS), Event::EndedSafe, Cause::None);
}

// --- US6: the weather clears mid-sequence ---

void test_safe_while_waiting_ends_without_a_close()
{
    Rig r;
    r.startUnsafe();
    r.in.weatherSafe = true;
    assertEvent(r.at(T0 + 5000), Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.parked = true;
    Counts c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
}

void test_safe_while_closing_lets_the_close_finish()
{
    Rig r;
    r.startAndClose();
    r.in.weatherSafe = true;
    Counts c = r.runTo(T0 + 40000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    r.in.roof = State::Closed;
    assertEvent(r.at(T0 + 40100), Event::Closed, Cause::None);
}

void test_flapping_verdict_keeps_requests_thirty_seconds_apart()
{
    Rig r;
    r.in.weatherSafe = false;
    uint64_t last = 0;
    bool any = false;
    int requests = 0;
    for (uint64_t t = T0; t < T0 + 600000; t += 100)
    {
        r.in.weatherSafe = ((t / 1000) % 2) == 0;
        const Decision d = r.at(t);
        if (d.action != Action::RequestPark)
            continue;
        if (any)
            TEST_ASSERT_TRUE(t - last >= PARK_REQUEST_GAP_MS);
        any = true;
        last = t;
        requests++;
    }
    TEST_ASSERT_TRUE(requests >= 2);
    TEST_ASSERT_TRUE(requests <= 21);
}

void test_flapping_verdict_still_raises_mount_not_parked_in_time()
{
    // Rain that comes and goes, N.I.N.A. not answering: the safe blips don't
    // restart the park time limit, so the alert comes when it runs out.
    Rig r;
    std::vector<Decision> alerts;
    uint64_t firstAlertMs = 0;
    for (uint64_t t = T0; t < T0 + 600000; t += 100)
    {
        r.in.weatherSafe = (t - T0) % 60000 >= 57000; // 3 s safe in every minute
        const Decision d = r.at(t);
        if (d.alert && firstAlertMs == 0)
            firstAlertMs = t;
        if (d.alert)
            alerts.push_back(d);
    }
    TEST_ASSERT_FALSE(alerts.empty());
    assertEvent(alerts.front(), Event::ParkNotConfirmed, Cause::ParkTimeout);
    // 240 s of unsafe time: the 3 s safe blips are left out of the clock.
    TEST_ASSERT_TRUE(firstAlertMs <= T0 + DEFAULT_PARK_TIMEOUT_MS + 15000);
}

void test_a_long_safe_spell_starts_a_fresh_attempt()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    r.in.weatherSafe = true;
    r.at(T0 + 50000);
    r.runTo(T0 + 50000 + REARM_SAFE_MS, 1000);
    r.in.weatherSafe = false;
    const uint64_t start = T0 + 50000 + REARM_SAFE_MS + 1000;
    TEST_ASSERT_EQUAL(Event::Started, r.at(start).event);
    TEST_ASSERT_EQUAL(0u, r.runTo(start + 60000 - 1, 1000).events.size());
    assertEvent(r.at(start + 60000), Event::ParkNotConfirmed, Cause::ParkTimeout);
}

void test_reminders_in_the_retry_wait_stop_when_safe()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.in.ninaConfigured = false;
    r.startUnsafe();
    TEST_ASSERT_TRUE(r.at(T0 + 100).alert); // N.I.N.A. not set up
    // Each new attempt fails again at once: logged, and alerted only every 30 minutes.
    Counts c = r.runTo(T0 + 100 + REMIND_MS - 1, 1000);
    TEST_ASSERT_EQUAL(0, c.alerts);
    c = r.runTo(T0 + 100 + REMIND_MS + RETRY_AFTER_MS, 1000);
    TEST_ASSERT_EQUAL(1, c.alerts);
    r.in.weatherSafe = true;
    r.at(T0 + 200 + REMIND_MS + RETRY_AFTER_MS);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    c = r.runTo(T0 + 3 * REMIND_MS, 1000);
    TEST_ASSERT_EQUAL(0, c.alerts);
}

void test_stand_down_rearm_counts_from_the_stop()
{
    Rig r;
    r.startAndClose();
    r.in.weatherSafe = true; // safe from here, while a long close runs on
    r.in.moveTimeoutMs = 900000;
    r.runTo(T0 + 4100 + REARM_SAFE_MS, 1000);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    r.in.personStops = 1;
    r.in.roof = State::Stopped;
    const uint64_t stop = T0 + 5000 + REARM_SAFE_MS;
    assertEvent(r.at(stop), Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_EQUAL(0u, r.runTo(stop + REARM_SAFE_MS - 1000, 1000).events.size());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    assertEvent(r.at(stop + REARM_SAFE_MS), Event::EndedSafe, Cause::None);
}

void test_a_restored_stand_down_holds_and_reminds_at_once()
{
    Sequence seq;
    seq.restore(Phase::HeldByPerson, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, seq.phase());
    Inputs in;
    in.nowMs = 3000;
    in.weatherSafe = false; // unknown at boot
    in.roof = State::Open;
    in.parked = true;
    const Decision d = seq.step(in);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_TRUE(d.reminder);
    for (in.nowMs = 3100; in.nowMs < 3600000; in.nowMs += 1000)
        TEST_ASSERT_EQUAL(Action::None, seq.step(in).action);
    TEST_ASSERT_EQUAL(Event::StoppedByPerson, seq.lastEvent());
    // Only the stand-downs are restored.
    Sequence other;
    other.restore(Phase::Closing, Event::CloseFailed, Cause::Refused);
    TEST_ASSERT_EQUAL(Phase::Idle, other.phase());
    other.restore(Phase::RetryWait, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_EQUAL(Phase::Idle, other.phase());
    other.restore(Phase::Halted, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_EQUAL(Phase::Halted, other.phase());
    TEST_ASSERT_EQUAL(Cause::TooLong, other.lastCause());
}

void test_a_resumed_attempt_counts_only_unsafe_time()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe(); // request at T0
    r.in.weatherSafe = true;
    r.at(T0 + 1000); // 1 s unsafe, then 9 minutes safe
    r.runTo(T0 + 1000 + 9 * 60000, 1000);
    r.in.weatherSafe = false;
    const uint64_t back = T0 + 2000 + 9 * 60000;
    const Decision d = r.at(back);
    TEST_ASSERT_EQUAL(Event::Started, d.event);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action); // asked again, not failed at once
    TEST_ASSERT_EQUAL(0u, r.runTo(back + 59000 - 1000, 1000).events.size());
    assertEvent(r.at(back + 60000), Event::ParkNotConfirmed, Cause::ParkTimeout);
}

void test_a_closed_roof_resets_the_alert_throttle()
{
    Rig r;
    r.in.roof = State::Error;
    TEST_ASSERT_TRUE(r.startUnsafe().alert);
    r.in.roof = State::Closing; // the owner cleared the fault and closes it
    r.in.moveTimeoutMs = 60000;
    r.runTo(T0 + 40000, 100);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    r.in.roof = State::Closed;
    assertEvent(r.at(T0 + 60000), Event::Closed, Cause::None);
    r.in.roof = State::Open; // reopened in good weather...
    r.in.weatherSafe = true;
    r.at(T0 + 120000);
    r.in.roof = State::Error; // ...and the same fault again in the next rain
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + 300000);
    assertEvent(d, Event::CannotClose, Cause::FaultLatched);
    TEST_ASSERT_TRUE(d.alert);
}

void test_a_hand_close_after_a_halt_resets_the_alert_throttle()
{
    Rig r;
    r.startAndClose();
    const uint64_t tripped = T0 + 4100 + 60000 + CLOSE_GUARD_SLACK_MS;
    TEST_ASSERT_TRUE(r.at(tripped).alert); // halted
    r.in.roof = State::Closing;            // the owner closes it by hand
    r.at(tripped + 600000);
    r.in.roof = State::Closed;
    assertEvent(r.at(tripped + 640000), Event::Closed, Cause::None);
    r.in.roof = State::Open; // reopened in good weather
    r.in.weatherSafe = true;
    r.at(tripped + 700000);
    r.in.weatherSafe = false; // the same failure in the next rain alerts at once
    r.startAndCloseFrom(tripped + 760000);
    const Decision d = r.at(tripped + 760000 + 4100 + 90000);
    assertEvent(d, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_TRUE(d.alert);
}

void test_a_retry_wait_stretched_by_safe_spells_is_capped_in_wall_time()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    r.startUnsafe();
    r.at(T0 + 60000); // failed: RetryWait
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    uint64_t retriedAt = 0;
    for (uint64_t t = T0 + 61000; t < T0 + 3 * 3600000ULL && retriedAt == 0; t += 1000)
    {
        r.in.weatherSafe = (t - T0) % 540000 >= 1000; // 1 s unsafe, then 9 min safe
        if (r.at(t).action == Action::RequestPark)
            retriedAt = t;
    }
    TEST_ASSERT_TRUE(retriedAt != 0);
    TEST_ASSERT_TRUE(retriedAt - (T0 + 60000) <= RETRY_AFTER_MS + REARM_SAFE_MS + 540000);
}

void test_a_closed_reading_flickering_at_rest_does_not_storm_alerts()
{
    Rig r;
    r.in.roof = State::Closed;
    r.in.weatherSafe = false;
    int alerts = 0;
    for (uint64_t t = T0; t < T0 + 600000; t += 100)
    {
        r.in.roof = ((t - T0) / 3000) % 2 == 0 ? State::Closed : State::Unknown; // 3 s each
        if (r.at(t).alert)
            alerts++;
    }
    TEST_ASSERT_EQUAL(1, alerts);
}

void test_an_attempt_stretched_by_safe_spells_is_capped_in_wall_time()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    uint64_t failedAt = 0;
    for (uint64_t t = T0; t < T0 + 3 * 3600000ULL && failedAt == 0; t += 1000)
    {
        r.in.weatherSafe = (t - T0) % 540000 >= 1000; // 1 s unsafe, then 9 min safe
        if (r.at(t).event == Event::ParkNotConfirmed)
            failedAt = t;
    }
    TEST_ASSERT_TRUE(failedAt != 0);
    TEST_ASSERT_TRUE(failedAt - T0 <= 2 * 60000 + REARM_SAFE_MS + 540000);
    // The alert names the cap that ran out, not the park limit.
    Rig again;
    again.seq.setParkTimeoutMs(60000);
    Decision fail;
    for (uint64_t t = T0; t < T0 + 3 * 3600000ULL && fail.event != Event::ParkNotConfirmed; t += 1000)
    {
        again.in.weatherSafe = (t - T0) % 540000 >= 1000;
        fail = again.at(t);
    }
    assertEvent(fail, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_EQUAL_UINT64(2 * 60000 + REARM_SAFE_MS, fail.limitMs);
}

void test_no_reminder_of_cannot_close_once_the_roof_can_move()
{
    Rig r;
    r.in.roof = State::Error;
    r.startUnsafe();
    r.in.roof = State::Open; // the fault cleared at the open magnet, no park
    r.at(T0 + 1000);
    TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
    Counts c = r.runTo(T0 + 1000 + RETRY_AFTER_MS - 1000, 1000);
    TEST_ASSERT_EQUAL(0, c.alerts);
    for (const Decision &e : c.events)
        TEST_ASSERT_FALSE(e.reminder);
}

void test_restore_ignores_values_out_of_range()
{
    Sequence seq;
    seq.restore(Phase::HeldByPerson, static_cast<Event>(200), Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(Phase::HeldByPerson, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(Phase::Halted, Event::CloseFailed, static_cast<Cause>(77));
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    Decision restored;
    restored.event = Event::CloseFailed;
    restored.cause = Cause::TooLong;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(restored, "stopped").message.c_str(), "didn't finish in time"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_persons_stop_while_closing_stands_down);
    RUN_TEST(test_persons_stop_while_waiting_or_backing_off_stands_down);
    RUN_TEST(test_stood_down_rearms_after_ten_minutes_safe);
    RUN_TEST(test_a_broken_safe_run_does_not_rearm);
    RUN_TEST(test_persons_close_while_stood_down_ends_it);
    RUN_TEST(test_stops_before_the_episode_are_ignored);
    RUN_TEST(test_the_count_at_start_up_is_history);
    RUN_TEST(test_unknown_position_stands_down_after_two_seconds);
    RUN_TEST(test_unknown_at_start_up_that_settles_starts_normally);
    RUN_TEST(test_fault_latched_cannot_close);
    RUN_TEST(test_a_stop_while_unable_to_move_stands_down_once_it_can);
    RUN_TEST(test_fault_while_waiting_stands_down);
    RUN_TEST(test_unknown_while_waiting_or_closing_stands_down_after_two_seconds);
    RUN_TEST(test_resumes_after_a_person_makes_the_position_known);
    RUN_TEST(test_cannot_move_ends_when_closed_or_safe_for_ten_minutes);
    RUN_TEST(test_safe_while_waiting_ends_without_a_close);
    RUN_TEST(test_safe_while_closing_lets_the_close_finish);
    RUN_TEST(test_flapping_verdict_keeps_requests_thirty_seconds_apart);
    RUN_TEST(test_flapping_verdict_still_raises_mount_not_parked_in_time);
    RUN_TEST(test_a_long_safe_spell_starts_a_fresh_attempt);
    RUN_TEST(test_reminders_in_the_retry_wait_stop_when_safe);
    RUN_TEST(test_stand_down_rearm_counts_from_the_stop);
    RUN_TEST(test_a_restored_stand_down_holds_and_reminds_at_once);
    RUN_TEST(test_a_resumed_attempt_counts_only_unsafe_time);
    RUN_TEST(test_a_closed_roof_resets_the_alert_throttle);
    RUN_TEST(test_a_hand_close_after_a_halt_resets_the_alert_throttle);
    RUN_TEST(test_a_retry_wait_stretched_by_safe_spells_is_capped_in_wall_time);
    RUN_TEST(test_a_closed_reading_flickering_at_rest_does_not_storm_alerts);
    RUN_TEST(test_an_attempt_stretched_by_safe_spells_is_capped_in_wall_time);
    RUN_TEST(test_no_reminder_of_cannot_close_once_the_roof_can_move);
    RUN_TEST(test_restore_ignores_values_out_of_range);
    return UNITY_END();
}
