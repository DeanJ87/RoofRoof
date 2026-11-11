// Spec 013: the close sequence with daylight as a second reason to close.
// The daylight close is spec 005's sequence step for step; a person's stop
// and a halt hold while the sun stays up; the weather turning unsafe re-arms
// a daylight-only stand-down; the alerts say daylight; modes off and alert
// leave the sequence exactly as spec 005 (SC-002).

#include "../close_rig/CloseRig.h"
#include "Daylight.h"
#include "RoofAlerts.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // Sun up with "alert and close" at T0, the weather safe and the roof open.
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

    // xorshift32, as test_close_properties.
    struct Random
    {
        uint32_t state;
        explicit Random(uint32_t seed)
            : state(seed * 2654435761u + 1u)
        {
        }
        uint32_t next()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        bool chance(uint32_t perThousand) { return next() % 1000 < perThousand; }
    };
} // namespace

// US2 AS1: the park first, the close once it has held.
void test_daylight_close_waits_for_the_park()
{
    Rig r;
    const Decision first = startDaylight(r);
    assertEvent(first, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, first.action);
    TEST_ASSERT_TRUE(first.daylight);
    TEST_ASSERT_TRUE(r.seq.daylightEpisode());
    r.in.parked = true;
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 100 + PARK_SETTLE_MS - 1).action);
    const Decision close = r.at(T0 + 100 + PARK_SETTLE_MS);
    TEST_ASSERT_EQUAL(Action::Close, close.action);
    TEST_ASSERT_TRUE(close.daylight);
    TEST_ASSERT_EQUAL(Event::None, r.seq.closeAnswered(CloseAnswer::Started).event);
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    r.at(T0 + 10000);
    r.in.roof = State::Closed;
    const Decision closed = r.at(T0 + 40000);
    assertEvent(closed, Event::Closed, Cause::None);
    TEST_ASSERT_TRUE(closed.daylight);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

// US2 AS2: the park not confirmed in time: open, critical, worded for daylight.
void test_daylight_park_timeout_alerts()
{
    Rig r;
    startDaylight(r);
    TEST_ASSERT_EQUAL(Event::None, r.at(T0 + DEFAULT_PARK_TIMEOUT_MS - 1).event);
    const Decision d = r.at(T0 + DEFAULT_PARK_TIMEOUT_MS);
    assertEvent(d, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_TRUE(d.daylight);
    const AlertText text = alertFor(d, "AtPark false");
    TEST_ASSERT_EQUAL_STRING("Mount didn't park", text.title.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "It is daylight and the park wasn't confirmed within 240 s, so the roof stays open. Park: AtPark false", text.message.c_str());
}

// US2 AS3, FR-014: a person's stop holds while the sun stays up.
void test_persons_stop_holds_while_the_sun_is_up()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.parked = true;
    Counts c = r.runTo(T0 + 6 * 3600000ULL, 1000);
    // Review: no wake-me reminder all day for a stop the person meant.
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(0u, c.events.size());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    Decision stopped;
    stopped.event = Event::StoppedByPerson;
    stopped.daylight = true;
    const AlertText text = alertFor(stopped, "");
    TEST_ASSERT_EQUAL_STRING(
        "A person stopped the roof during the automatic close. It stays open in daylight until someone closes it or the sun has "
        "been down (or the daylight close off) for 10 minutes.",
        text.message.c_str());
}

// FR-014: re-armed once nothing has wanted a close for 10 minutes.
void test_stand_down_rearms_after_ten_minutes_without_daylight()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.daylight = false;
    r.at(T0 + 2000);
    TEST_ASSERT_EQUAL(0u, r.runTo(T0 + 2000 + REARM_SAFE_MS - 1000, 1000).events.size());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    assertEvent(r.at(T0 + 2000 + REARM_SAFE_MS), Event::EndedSafe, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

// A halted daylight close stays halted while the sun is up.
void test_halt_holds_while_the_sun_is_up()
{
    Rig r;
    r.in.weatherSafe = true;
    r.in.daylight = true;
    r.in.parked = true;
    r.at(T0);
    r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 100 + PARK_SETTLE_MS).action);
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    r.at(T0 + 5000);
    const Decision halt = r.at(T0 + 5000 + 60000 + CLOSE_GUARD_SLACK_MS);
    assertEvent(halt, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_EQUAL(Action::Stop, halt.action);
    TEST_ASSERT_TRUE(halt.daylight);
    r.in.roof = State::Stopped;
    Counts c = r.runTo(T0 + 4 * 3600000ULL, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
}

// FR-013 / US3 AS2: the weather turning unsafe re-arms a daylight stand-down
// on the same step: the weather close starts at once.
void test_weather_rearms_a_daylight_stand_down()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 31000); // after the first request's gap
    r.at(T0 + 60000);
    r.in.weatherSafe = false;
    // One unsafe sample (a stale poll) doesn't end the person's stop: 30 s must pass.
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 61000).action);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 61000 + WEATHER_REARM_CONFIRM_MS - 1).action);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    const Decision d = r.at(T0 + 61000 + WEATHER_REARM_CONFIRM_MS);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    TEST_ASSERT_FALSE(d.daylight);
    TEST_ASSERT_FALSE(r.seq.daylightEpisode());
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    // A person can stand the weather close down again.
    standDownByPerson(r, T0 + 92000);
    r.in.weatherSafe = true;
    r.at(T0 + 93000);
    r.in.weatherSafe = false;
    r.runTo(T0 + 200000, 1000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase()); // a weather episode: no re-arm
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_weather_rearms_a_daylight_back_off_and_halt()
{
    {
        Rig r;
        r.seq.setParkTimeoutMs(60000);
        startDaylight(r);
        r.at(T0 + 60000);
        TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
        r.in.weatherSafe = false;
        const Decision d = r.at(T0 + 61000);
        assertEvent(d, Event::Started, Cause::None);
        TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    }
    {
        Rig r;
        r.in.weatherSafe = true;
        r.in.daylight = true;
        r.in.parked = true;
        r.at(T0);
        r.at(T0 + 100);
        r.at(T0 + 100 + PARK_SETTLE_MS);
        r.seq.closeAnswered(CloseAnswer::Started);
        r.in.roof = State::Closing;
        r.in.moveTimeoutMs = 60000;
        r.at(T0 + 5000);
        r.at(T0 + 5000 + 60000 + CLOSE_GUARD_SLACK_MS);
        r.in.roof = State::Stopped;
        r.at(T0 + 200000);
        TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
        r.in.weatherSafe = false;
        r.at(T0 + 201000);
        TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    }
}

// A short unsafe spell, then safe again: the stand-down stays; a new stop restarts the wait.
void test_short_unsafe_spell_keeps_the_stand_down()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    r.in.weatherSafe = false;
    r.runTo(T0 + 20000, 1000);
    r.in.weatherSafe = true;
    r.at(T0 + 21000);
    r.in.weatherSafe = false;
    r.runTo(T0 + 45000, 1000); // 24 s unsafe since the break
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.in.personStops++;
    r.at(T0 + 46000);
    r.runTo(T0 + 75000, 1000); // 29 s since the new stop
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.at(T0 + 76000);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
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

// A person's stop seen on the very step the weather turns unsafe wins.
void test_persons_stop_on_the_same_step_wins()
{
    Rig r;
    startDaylight(r);
    r.at(T0 + 1000);
    r.in.personStops++;
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + 2000);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_FALSE(d.daylight); // now a weather episode
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

// US3 AS3: an attempt waiting or a close under way carries on; the episode becomes weather.
void test_weather_during_a_daylight_attempt_carries_on()
{
    Rig r;
    startDaylight(r);
    r.at(T0 + 1000);
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + 2000);
    TEST_ASSERT_EQUAL(Event::None, d.event);
    TEST_ASSERT_FALSE(d.daylight);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    // The weather clearing while daylight still wants a close: the attempt goes on.
    r.in.weatherSafe = true;
    TEST_ASSERT_EQUAL(Event::None, r.at(T0 + 3000).event);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    TEST_ASSERT_FALSE(r.seq.daylightEpisode()); // stays a weather episode
}

// Clarification: a weather stand-down stays while daylight wants a close.
void test_weather_stand_down_stays_while_the_sun_is_up()
{
    Rig r;
    r.startUnsafe();
    standDownByPerson(r, T0 + 1000);
    r.in.daylight = true;
    r.in.weatherSafe = true;
    Counts c = r.runTo(T0 + 3 * 3600000ULL, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes + c.stops);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    r.in.daylight = false;
    r.runTo(r.in.nowMs + REARM_SAFE_MS + 2000, 1000);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
}

// US2 AS5: the mode changed while waiting for the park: the attempt ends without a close.
void test_daylight_ending_ends_a_waiting_attempt()
{
    Rig r;
    startDaylight(r);
    r.in.daylight = false;
    assertEvent(r.at(T0 + 1000), Event::EndedSafe, Cause::None);
    r.in.parked = true;
    Counts c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes);
}

// Edge cases: unknown position or a fault at sunrise: can't close, critical, daylight wording.
void test_daylight_cannot_move()
{
    Rig r;
    r.in.roof = State::Error;
    const Decision d = startDaylight(r);
    assertEvent(d, Event::CannotClose, Cause::FaultLatched);
    TEST_ASSERT_TRUE(d.daylight);
    TEST_ASSERT_EQUAL_STRING("It is daylight and a fault is latched. Fault: jam", alertFor(d, "jam").message.c_str());
    Rig n;
    n.in.ninaConfigured = false;
    startDaylight(n);
    const Decision nina = n.at(T0 + 100);
    assertEvent(nina, Event::ParkNotConfirmed, Cause::NinaNotConfigured);
    TEST_ASSERT_EQUAL_STRING(
        "It is daylight and N.I.N.A. isn't set up (park.nina.url), so the park can't be confirmed. The roof stays open.",
        alertFor(nina, "").message.c_str());
}

// The weather wording is spec 005's, word for word, without daylight.
void test_weather_wording_unchanged()
{
    Decision d;
    d.event = Event::CannotClose;
    d.cause = Cause::PositionUnknown;
    TEST_ASSERT_EQUAL_STRING(
        "The weather is unsafe and the roof's position is unknown. A person must move it to a magnet.", alertFor(d, "").message.c_str());
    d.event = Event::StoppedByPerson;
    d.cause = Cause::None;
    TEST_ASSERT_EQUAL_STRING(
        "A person stopped the roof during the automatic close. It stays open while unsafe until someone closes it or the weather has "
        "been safe for 10 minutes.",
        alertFor(d, "").message.c_str());
}

// A daylight stand-down survives a restart, and the weather still re-arms it after.
void test_restart_keeps_the_daylight_episode()
{
    Rig r;
    startDaylight(r);
    standDownByPerson(r, T0 + 1000);
    const Sequence::Saved saved = r.seq.saved();
    TEST_ASSERT_TRUE(saved.daylight);
    Sequence fresh;
    fresh.restore(saved);
    Inputs in = r.in;
    in.nowMs = T0 + 500000;
    in.personStops = 0;
    const Decision first = fresh.step(in);
    TEST_ASSERT_FALSE(first.reminder); // no wake-me reminder for a daylight stop
    TEST_ASSERT_TRUE(first.daylight);
    TEST_ASSERT_TRUE(fresh.daylightEpisode());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, fresh.phase());
    in.weatherSafe = false;
    in.nowMs += 1000;
    fresh.step(in);
    in.nowMs += WEATHER_REARM_CONFIRM_MS;
    const Decision d = fresh.step(in);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Phase::WaitPark, fresh.phase());
}

// SC-002: with the mode off or alert, daylight never asks for a close
// (closes() is false whatever the sun, the site and the clock), so the
// sequence gets spec 005's inputs. That the sequence then decides as spec 005
// did is proven by spec 005's own suites, which run unchanged; this test
// proves the first half and that no decision is tagged daylight.
void test_off_and_alert_leave_the_sequence_unchanged()
{
    for (uint32_t seed = 1; seed <= 5000; ++seed)
    {
        Random rnd(seed);
        Daylight::Watch watch;
        Daylight::Settings settings;
        settings.mode = rnd.chance(500) ? Daylight::Mode::Off : Daylight::Mode::Alert;
        settings.limitDeg = static_cast<float>(static_cast<int>(rnd.next() % 48) - 18);
        settings.locationSet = rnd.chance(800);
        settings.latitude = static_cast<double>(rnd.next() % 180) - 90.0;
        settings.longitude = static_cast<double>(rnd.next() % 360) - 180.0;
        Sequence plain, withDaylight;
        Inputs in;
        in.nowMs = 1000;
        int64_t epoch = 1718928000 + static_cast<int64_t>(rnd.next() % 86400);
        for (int step = 0; step < 300; ++step)
        {
            in.nowMs += 100 + rnd.next() % 60000;
            epoch += 600;
            if (rnd.chance(50))
                in.weatherSafe = !in.weatherSafe;
            if (rnd.chance(50))
                in.parked = !in.parked;
            if (rnd.chance(20))
                in.personStops++;
            if (rnd.chance(30))
                in.roof = rnd.chance(500) ? State::Open : State::Closed;
            const Daylight::View view = watch.update(settings, rnd.chance(900), epoch, rnd.chance(100));
            TEST_ASSERT_FALSE(view.closes());
            Inputs other = in;
            other.daylight = view.closes();
            in.daylight = false;
            const Decision a = plain.step(in);
            const Decision b = withDaylight.step(other);
            TEST_ASSERT_EQUAL(static_cast<int>(a.action), static_cast<int>(b.action));
            TEST_ASSERT_EQUAL(static_cast<int>(a.event), static_cast<int>(b.event));
            TEST_ASSERT_EQUAL(static_cast<int>(a.cause), static_cast<int>(b.cause));
            TEST_ASSERT_EQUAL(a.alert, b.alert);
            TEST_ASSERT_FALSE(b.daylight);
            TEST_ASSERT_EQUAL(static_cast<int>(plain.phase()), static_cast<int>(withDaylight.phase()));
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_daylight_close_waits_for_the_park);
    RUN_TEST(test_daylight_park_timeout_alerts);
    RUN_TEST(test_persons_stop_holds_while_the_sun_is_up);
    RUN_TEST(test_stand_down_rearms_after_ten_minutes_without_daylight);
    RUN_TEST(test_halt_holds_while_the_sun_is_up);
    RUN_TEST(test_weather_rearms_a_daylight_stand_down);
    RUN_TEST(test_weather_rearms_a_daylight_back_off_and_halt);
    RUN_TEST(test_short_unsafe_spell_keeps_the_stand_down);
    RUN_TEST(test_weather_failure_alerts_after_a_daylight_failure);
    RUN_TEST(test_flipped_episode_closes_as_weather);
    RUN_TEST(test_persons_stop_on_the_same_step_wins);
    RUN_TEST(test_weather_during_a_daylight_attempt_carries_on);
    RUN_TEST(test_weather_stand_down_stays_while_the_sun_is_up);
    RUN_TEST(test_daylight_ending_ends_a_waiting_attempt);
    RUN_TEST(test_daylight_cannot_move);
    RUN_TEST(test_weather_wording_unchanged);
    RUN_TEST(test_restart_keeps_the_daylight_episode);
    RUN_TEST(test_off_and_alert_leave_the_sequence_unchanged);
    return UNITY_END();
}
