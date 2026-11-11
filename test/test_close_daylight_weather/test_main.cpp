// Spec 013 FR-013 and FR-014, review findings: how unsafe weather takes over
// from a daylight-only episode (the 30 s count against a person's stop or a
// halt, fresh attempts and alerts, the count across a restart).

#include "../close_rig/CloseRig.h"
#include "Daylight.h"
#include "RoofAlerts.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Decision startDaylight(Rig &r)
    {
        r.in.weatherSafe = true;
        r.in.daylight = true;
        return r.at(T0);
    }

    void standDownByPerson(Rig &r, uint64_t atMs)
    {
        r.in.personStops++;
        assertEvent(r.at(atMs), Event::StoppedByPerson, Cause::None);
        TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    }
} // namespace

// The 30 s are added up over unsafe spells since the stop, so a flapping
// verdict still ends the stand-down; a new stop starts the count again.
void test_unsafe_spells_add_up_to_end_the_stand_down()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.weatherSafe = false;
    r.at(T0 + 2000);
    r.runTo(T0 + 22000, 1000); // 20 s unsafe
    r.in.weatherSafe = true;
    r.runTo(T0 + 27000, 1000); // 5 s safe
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.in.weatherSafe = false;
    r.at(T0 + 28000);
    // Each step after an unsafe one counts: 21 s from the first spell (to the
    // first safe step), so 29 s in all at 36 s.
    r.runTo(T0 + 36000, 1000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.at(T0 + 37000); // 30 s
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_new_stop_starts_the_count_again()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.weatherSafe = false;
    r.at(T0 + 2000);
    r.runTo(T0 + 25000, 1000);
    r.in.personStops++;
    r.at(T0 + 26000);
    r.runTo(T0 + 55000, 1000); // 29 s since the new stop
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.at(T0 + 56000);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

// Review round 3: unsafe 20 s, safe 5 s, for hours: the weather close starts.
void test_flapping_weather_ends_a_daylight_stand_down()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 6000);
    uint64_t t = T0 + 60000;
    bool started = false;
    for (int cycle = 0; cycle < 20 && !started; ++cycle)
    {
        r.in.weatherSafe = false;
        for (uint64_t end = t + 20000; t < end; t += 1000)
            started = started || r.at(t).event == Event::Started;
        r.in.weatherSafe = true;
        for (uint64_t end = t + 5000; t < end; t += 1000)
            r.at(t);
    }
    TEST_ASSERT_TRUE(started);
}

// A person's daylight stand-down reminds again while the weather itself is unsafe.
void test_daylight_stand_down_reminds_while_unsafe()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.weatherSafe = false;
    r.in.personStops++; // stopped again every 20 s: the stand-down holds
    Counts c;
    for (uint64_t t = T0 + 2000; t < T0 + REMIND_MS + 60000; t += 1000)
    {
        if ((t / 1000) % 20 == 0)
            r.in.personStops++;
        Rig::note(c, r.at(t));
    }
    bool reminded = false;
    for (const Decision &d : c.events)
        reminded = reminded || (d.event == Event::StoppedByPerson && d.reminder);
    TEST_ASSERT_TRUE(reminded);
}

// Review round 3: after a daylight episode ended by 10 minutes without a
// reason, the next weather episode's identical alert isn't throttled.
void test_weather_alerts_after_a_daylight_episode_ended()
{
    Rig r;
    r.in.roof = State::Error;
    const Decision daylight = startDaylight(r);
    TEST_ASSERT_TRUE(daylight.alert);
    r.in.daylight = false;
    r.runTo(T0 + 2000 + REARM_SAFE_MS + 5000, 1000);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.weatherSafe = false;
    const Decision d = r.at(r.in.nowMs + 1000);
    assertEvent(d, Event::CannotClose, Cause::FaultLatched);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_FALSE(d.daylight);
}

// Review: the weather close's critical alert isn't swallowed by the daylight
// close's identical one just before (the throttle starts afresh).
void test_weather_failure_alerts_after_a_daylight_failure()
{
    Rig r;
    r.in.ninaConfigured = false;
    startDaylight(r);
    const Decision daylight = r.at(T0 + 100);
    assertEvent(daylight, Event::ParkNotConfirmed, Cause::NinaNotConfigured);
    TEST_ASSERT_TRUE(daylight.alert);
    TEST_ASSERT_TRUE(daylight.daylight);
    r.runTo(T0 + 20 * 60000, 1000);
    r.in.weatherSafe = false; // rain 20 minutes later
    Counts c = r.runTo(T0 + 20 * 60000 + 5000, 1000);
    bool alerted = false;
    for (const Decision &d : c.events)
        if (d.event == Event::ParkNotConfirmed && d.alert && !d.daylight)
            alerted = true;
    TEST_ASSERT_TRUE(alerted);
}

// Review round 2: a daylight "can't close by itself", then rain: the weather's
// own alert goes out at once, not with the next 30-minute reminder.
void test_weather_alerts_at_once_after_a_daylight_cannot_move()
{
    Rig r;
    r.in.roof = State::Error;
    assertEvent(startDaylight(r), Event::CannotClose, Cause::FaultLatched);
    r.runTo(T0 + 20000, 1000);
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + 21000);
    assertEvent(d, Event::CannotClose, Cause::FaultLatched);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_FALSE(d.daylight);
    TEST_ASSERT_EQUAL_STRING("The weather is unsafe and a fault is latched. Fault: jam", alertFor(d, "jam").message.c_str());
}

// Review round 2: a daylight attempt that failed, paused when daylight ended,
// resumed by rain: the weather's failure alerts.
void test_weather_alerts_after_a_paused_daylight_failure()
{
    Rig r;
    r.seq.setParkTimeoutMs(60000);
    startDaylight(r);
    const Decision failed = r.at(T0 + 60000);
    assertEvent(failed, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_TRUE(failed.alert);
    r.in.daylight = false; // the mode set to alert
    assertEvent(r.at(T0 + 61000), Event::EndedSafe, Cause::None);
    r.in.weatherSafe = false; // rain a minute later
    Counts c = r.runTo(T0 + 61000 + RETRY_AFTER_MS + 120000, 1000);
    bool alerted = false;
    for (const Decision &d : c.events)
        if (d.event == Event::ParkNotConfirmed && d.alert && !d.reminder && !d.daylight)
            alerted = true;
    TEST_ASSERT_TRUE(alerted);
}

// Review: a daylight episode that became a weather one closes as a weather one.
void test_flipped_episode_closes_as_weather()
{
    Rig r;
    startDaylight(r);
    r.in.weatherSafe = false;
    r.at(T0 + 1000);
    r.in.parked = true;
    r.at(T0 + 2000);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 2000 + PARK_SETTLE_MS).action);
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    r.at(T0 + 10000);
    r.in.roof = State::Closed;
    const Decision closed = r.at(T0 + 30000);
    assertEvent(closed, Event::Closed, Cause::None);
    TEST_ASSERT_FALSE(closed.daylight);
    TEST_ASSERT_TRUE(closedAfterAttempt(Phase::Closing, closed.event));
}

// Review round 4: a daylight back-off ended by daylight going, then rain: a
// fresh weather attempt at once, not the rest of the daylight back-off.
void test_rain_after_a_daylight_back_off_starts_afresh()
{
    Rig r;
    startDaylight(r);
    assertEvent(r.at(T0 + DEFAULT_PARK_TIMEOUT_MS), Event::ParkNotConfirmed, Cause::ParkTimeout);
    r.in.daylight = false;
    r.at(T0 + DEFAULT_PARK_TIMEOUT_MS + 10000);
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + DEFAULT_PARK_TIMEOUT_MS + 40000);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    TEST_ASSERT_FALSE(d.daylight);
}

// Review round 4: a refused daylight close doesn't hold back the weather's
// close of a parked mount.
void test_daylight_close_refusal_does_not_hold_back_the_weather()
{
    Rig r;
    r.in.parked = true;
    startDaylight(r);
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 100 + PARK_SETTLE_MS).action);
    assertEvent(r.seq.closeAnswered(CloseAnswer::Refused), Event::CloseFailed, Cause::Refused);
    r.in.weatherSafe = false;
    Counts c = r.runTo(T0 + 30000, 100);
    TEST_ASSERT_TRUE(c.closes >= 1);
}

// Review round 4: the weather's count against a daylight stand-down outlives a restart.
void test_count_survives_a_restart()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.weatherSafe = false;
    r.at(T0 + 2000);
    r.runTo(T0 + 22000, 1000); // 20 s
    const Sequence::Saved saved = r.seq.saved();
    TEST_ASSERT_EQUAL_UINT32(20000, static_cast<uint32_t>(saved.unsafeMs));
    Sequence fresh;
    fresh.restore(saved);
    Inputs in = r.in;
    in.personStops = 0;
    for (uint64_t t = T0 + 30000; t <= T0 + 39000; t += 1000)
    {
        in.nowMs = t;
        fresh.step(in);
    }
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, fresh.phase()); // 20 s kept + 9 s since the restart's first step
    in.nowMs = T0 + 40000;
    fresh.step(in);
    TEST_ASSERT_EQUAL(Phase::WaitPark, fresh.phase());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_unsafe_spells_add_up_to_end_the_stand_down);
    RUN_TEST(test_new_stop_starts_the_count_again);
    RUN_TEST(test_flapping_weather_ends_a_daylight_stand_down);
    RUN_TEST(test_daylight_stand_down_reminds_while_unsafe);
    RUN_TEST(test_weather_alerts_after_a_daylight_episode_ended);
    RUN_TEST(test_weather_failure_alerts_after_a_daylight_failure);
    RUN_TEST(test_weather_alerts_at_once_after_a_daylight_cannot_move);
    RUN_TEST(test_weather_alerts_after_a_paused_daylight_failure);
    RUN_TEST(test_flipped_episode_closes_as_weather);
    RUN_TEST(test_rain_after_a_daylight_back_off_starts_afresh);
    RUN_TEST(test_daylight_close_refusal_does_not_hold_back_the_weather);
    RUN_TEST(test_count_survives_a_restart);
    return UNITY_END();
}
