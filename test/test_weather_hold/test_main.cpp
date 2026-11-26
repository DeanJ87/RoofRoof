// Spec 030 (FR-001..FR-004, SC-001): the weather hold. A blip never starts the
// automatic close; rain does at once, and so does a failed rain sensor
// (constitution 10.1.0); a silent SQMeter is still held; a flapping verdict
// closes once. The real
// sequence is stepped every 100 ms with the roof open and the park confirmed,
// the weather seen through the hold as CloseOnUnsafe does.

#include <unity.h>
#include <cstring>
#include <vector>

#include "CloseSequence.h"
#include "Config.h" // for the library finder: RoofLogic (Roof::State) builds with ConfigModel
#include "WeatherHold.h"

using namespace SQM;
using namespace SQM::CloseSeq;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t STEP_MS = 100;
    constexpr uint64_t T0 = 1000000;

    // What a run produced.
    struct Tally
    {
        int requests = 0;
        int closes = 0;
        int stops = 0;
        std::vector<Event> events;
        uint64_t firstRequestMs = 0;
    };

    // The sequence behind the hold, the roof open and the mount parked.
    struct HoldRig
    {
        Sequence seq;
        WeatherHold hold;
        Inputs in;
        uint64_t holdMs = UNSAFE_HOLD_MS;
        uint32_t reasons = 0; // the verdict's reasons, mapped as CloseOnUnsafe does
        bool overrideActive = false;
        // The roof follows the close: Closing once commanded, Closed this long after.
        bool simulateRoof = true;
        uint64_t closeTakesMs = 20000;
        uint64_t closingSinceMs = 0;
        Tally tally;

        HoldRig()
        {
            in.nowMs = T0;
            in.weatherSafe = true;
            in.roof = State::Open;
            in.parked = true;
            in.moveTimeoutMs = 60000;
            stepOnce(true);
        }

        void stepOnce(bool weatherSafe)
        {
            WeatherHold::Step s;
            s.nowMs = in.nowMs;
            s.weatherSafe = weatherSafe;
            s.atOnce = skipsHold(reasons);
            s.overrideActive = overrideActive;
            s.holdMs = holdMs;
            Inputs given = in;
            given.weatherSafe = hold.update(s);
            if (simulateRoof && in.roof == State::Closing && in.nowMs - closingSinceMs >= closeTakesMs)
                given.roof = in.roof = State::Closed;
            note(seq.step(given));
        }

        void note(const Decision &d)
        {
            if (d.action == Action::RequestPark)
            {
                if (tally.requests == 0)
                    tally.firstRequestMs = in.nowMs;
                tally.requests++;
            }
            if (d.action == Action::Stop)
                tally.stops++;
            if (d.event != Event::None)
                tally.events.push_back(d.event);
            if (d.action == Action::Close)
            {
                tally.closes++;
                seq.closeAnswered(CloseAnswer::Started);
                if (simulateRoof)
                {
                    in.roof = State::Closing;
                    closingSinceMs = in.nowMs;
                }
            }
        }

        // `ms` of steps with the weather `safe`.
        void run(uint64_t ms, bool safe)
        {
            for (uint64_t t = 0; t < ms; t += STEP_MS)
            {
                in.nowMs += STEP_MS;
                stepOnce(safe);
            }
        }

        bool saw(Event event) const
        {
            for (Event e : tally.events)
                if (e == event)
                    return true;
            return false;
        }
    };

    void assertNothingMoved(const HoldRig &r)
    {
        TEST_ASSERT_EQUAL(0, r.tally.requests);
        TEST_ASSERT_EQUAL(0, r.tally.closes);
        TEST_ASSERT_EQUAL(0, r.tally.stops);
        TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    }

    // The weather verdict from SQMeter over HTTP (staleSeconds 30), as the poller records it.
    struct Sqmeter
    {
        Safety::WeatherInputs w;

        Sqmeter()
        {
            w.http.enabled = true;
            w.httpLimitMs = 30000;
            w.startedAtMs = 0;
        }
        // `flags`: SQMeter's reasonFlags when unsafe (bit 4 cloud cover by default).
        void reply(uint64_t nowMs, bool safe, uint32_t flags = 1u << 4)
        {
            Safety::SqmeterReply r;
            r.safe = safe;
            r.reasonFlags = safe ? 0 : flags;
            w.http.recordGood(r, nowMs);
        }
        Safety::WeatherVerdict at(uint64_t nowMs)
        {
            w.nowMs = nowMs;
            return Safety::evaluateWeather(w);
        }
    };

    // Steps the rig for `ms` with SQMeter answering every 5 s unless `silent`
    // (safe, or unsafe with `flags`), the hold fed the verdict's reasons.
    void runSqmeter(HoldRig &r, Sqmeter &q, uint64_t ms, bool silent, bool safe = true, uint32_t flags = 1u << 4)
    {
        for (uint64_t t = 0; t < ms; t += STEP_MS)
        {
            r.in.nowMs += STEP_MS;
            if (!silent && r.in.nowMs % 5000 == 0)
                q.reply(r.in.nowMs, safe, flags);
            const Safety::WeatherVerdict v = q.at(r.in.nowMs);
            r.reasons = v.reasons;
            r.stepOnce(v.safe());
        }
    }
} // namespace

// --- the hold on its own ---

void test_hold_zero_passes_the_weather_through(void)
{
    WeatherHold hold;
    WeatherHold::Step s;
    s.holdMs = 0;
    s.weatherSafe = false;
    TEST_ASSERT_FALSE(hold.update(s));
    TEST_ASSERT_TRUE(hold.fired());
    s.nowMs = 100;
    s.weatherSafe = true;
    TEST_ASSERT_TRUE(hold.update(s));
    TEST_ASSERT_FALSE(hold.fired());
}

void test_hold_counts_and_reports_what_is_left(void)
{
    WeatherHold hold;
    WeatherHold::Step s;
    s.weatherSafe = true;
    TEST_ASSERT_TRUE(hold.update(s));
    TEST_ASSERT_EQUAL_UINT64(WeatherHold::NOT_HOLDING, hold.leftMs());
    s.weatherSafe = false;
    for (int i = 1; i <= 100; ++i)
    {
        s.nowMs = i * STEP_MS;
        TEST_ASSERT_TRUE(hold.update(s));
    }
    TEST_ASSERT_EQUAL_UINT64(10000, hold.countedMs());
    TEST_ASSERT_EQUAL_UINT64(50000, hold.leftMs());
    TEST_ASSERT_FALSE(hold.fired());
    // A late step adds at most a second.
    s.nowMs += 30000;
    TEST_ASSERT_TRUE(hold.update(s));
    TEST_ASSERT_EQUAL_UINT64(11000, hold.countedMs());
    // A safe step: the count is kept, but nothing counts down.
    s.nowMs += STEP_MS;
    s.weatherSafe = true;
    TEST_ASSERT_TRUE(hold.update(s));
    TEST_ASSERT_EQUAL_UINT64(11000, hold.countedMs());
    TEST_ASSERT_EQUAL_UINT64(WeatherHold::NOT_HOLDING, hold.leftMs());
}

void test_hold_starts_fired_after_a_restart(void)
{
    WeatherHold hold;
    hold.startFired();
    WeatherHold::Step s;
    s.weatherSafe = true;
    for (uint64_t t = 0; t < 59900; t += STEP_MS)
    {
        s.nowMs = t;
        TEST_ASSERT_FALSE(hold.update(s));
    }
    s.nowMs = 60000;
    TEST_ASSERT_TRUE(hold.update(s));
    TEST_ASSERT_FALSE(hold.fired());
}

void test_lowering_the_hold_below_the_count_fires_it(void)
{
    WeatherHold hold;
    WeatherHold::Step s;
    s.weatherSafe = false;
    for (uint64_t t = 0; t <= 30000; t += STEP_MS)
    {
        s.nowMs = t;
        hold.update(s);
    }
    TEST_ASSERT_FALSE(hold.fired());
    s.nowMs += STEP_MS;
    s.holdMs = 20000;
    TEST_ASSERT_FALSE(hold.update(s));
    TEST_ASSERT_TRUE(hold.fired());
}

void test_policy_carries_the_setting(void)
{
    Config cfg;
    TEST_ASSERT_EQUAL_UINT64(60000, policyFrom(cfg).unsafeHoldMs);
    cfg.autoClose.unsafeHoldSeconds = 0;
    TEST_ASSERT_EQUAL_UINT64(0, policyFrom(cfg).unsafeHoldMs);
    cfg.autoClose.unsafeHoldSeconds = 9999;
    TEST_ASSERT_EQUAL_UINT64(300000, policyFrom(cfg).unsafeHoldMs);
}

// --- SC-001 with the real sequence ---

void test_short_unsafe_spells_never_move_the_roof(void)
{
    for (uint64_t ms : {2000ull, 10000ull, 59000ull})
    {
        HoldRig r;
        r.run(ms, false);
        r.run(180000, true);
        assertNothingMoved(r);
        TEST_ASSERT_EQUAL(0u, r.tally.events.size());
        TEST_ASSERT_EQUAL_UINT64(0, r.hold.countedMs()); // forgotten after the safe spell
    }
}

void test_a_silent_sqmeter_never_moves_the_roof_within_seconds(void)
{
    for (uint64_t ms : {3000ull, 15000ull})
    {
        HoldRig r;
        Sqmeter q;
        q.reply(r.in.nowMs, true);
        runSqmeter(r, q, 20000, false);
        runSqmeter(r, q, ms, true);
        runSqmeter(r, q, 120000, false);
        assertNothingMoved(r);
    }
}

void test_a_silent_sqmeter_closes_after_stale_plus_hold(void)
{
    HoldRig r;
    Sqmeter q;
    q.reply(r.in.nowMs, true);
    runSqmeter(r, q, 20000, false);
    const uint64_t lastReply = r.in.nowMs - r.in.nowMs % 5000;
    runSqmeter(r, q, 100000, true);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
    // Stale after 30 s, then the 60 s hold: about 90 s after the last reply.
    TEST_ASSERT_UINT64_WITHIN(1000, lastReply + 90000, r.tally.firstRequestMs);
    TEST_ASSERT_EQUAL(1, r.tally.closes);
}

void test_61_s_unsafe_starts_the_close(void)
{
    HoldRig r;
    const uint64_t start = r.in.nowMs;
    r.run(59500, false);
    TEST_ASSERT_EQUAL(0, r.tally.requests);
    r.run(1500, false);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
    TEST_ASSERT_UINT64_WITHIN(200, start + 60000, r.tally.firstRequestMs);
    r.run(10000, false);
    TEST_ASSERT_EQUAL(1, r.tally.closes);
}

void test_rain_starts_the_close_at_once(void)
{
    HoldRig r;
    r.reasons = Safety::ReasonRain;
    r.run(STEP_MS, false);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    r.run(5000, false);
    TEST_ASSERT_EQUAL(1, r.tally.closes);
}

void test_a_flapping_verdict_closes_exactly_once(void)
{
    HoldRig r;
    for (int i = 0; i < 18; ++i) // two minutes, then one more
    {
        r.run(5000, false);
        r.run(5000, true);
    }
    TEST_ASSERT_EQUAL(1, r.tally.closes);
    TEST_ASSERT_EQUAL(0, r.tally.stops);
    TEST_ASSERT_EQUAL(State::Closed, r.in.roof);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    TEST_ASSERT_TRUE(r.saw(Event::Closed));
}

void test_a_flapping_verdict_reaches_the_hold_in_about_two_minutes(void)
{
    HoldRig r;
    r.simulateRoof = false;
    r.in.parked = false; // no close: only when the hold fires
    for (int i = 0; i < 11; ++i)
    {
        r.run(5000, false);
        r.run(5000, true);
    }
    TEST_ASSERT_EQUAL(0, r.tally.requests);
    r.run(5000, false);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
    // The safe blips don't call it off.
    for (int i = 0; i < 6; ++i)
    {
        r.run(5000, true);
        r.run(5000, false);
    }
    TEST_ASSERT_FALSE(r.saw(Event::EndedSafe));
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_safe_for_the_hold_while_waiting_calls_the_attempt_off(void)
{
    HoldRig r;
    r.in.parked = false; // N.I.N.A. hasn't parked yet
    r.run(61000, false);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    r.run(59000, true);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    r.run(2000, true);
    TEST_ASSERT_TRUE(r.saw(Event::EndedSafe));
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.parked = true; // the park arriving later closes nothing
    r.run(60000, true);
    TEST_ASSERT_EQUAL(0, r.tally.closes);
}

void test_safe_weather_does_not_stop_a_running_close(void)
{
    HoldRig r;
    r.closeTakesMs = 50000;
    r.run(65000, false);
    TEST_ASSERT_EQUAL(1, r.tally.closes);
    TEST_ASSERT_EQUAL(State::Closing, r.in.roof);
    r.run(70000, true);
    TEST_ASSERT_EQUAL(0, r.tally.stops);
    TEST_ASSERT_EQUAL(State::Closed, r.in.roof);
    TEST_ASSERT_TRUE(r.saw(Event::Closed));
    TEST_ASSERT_FALSE(r.saw(Event::EndedSafe));
}

void test_hold_zero_is_the_old_behaviour(void)
{
    HoldRig r;
    r.holdMs = 0;
    r.run(STEP_MS, false);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
}

void test_an_override_resets_the_hold(void)
{
    HoldRig r;
    r.in.parked = false;
    r.run(50000, false);
    TEST_ASSERT_EQUAL_UINT64(50000, r.hold.countedMs());
    // The person's override makes the weather safe after the override: reset.
    r.overrideActive = true;
    r.run(STEP_MS, true);
    TEST_ASSERT_EQUAL_UINT64(0, r.hold.countedMs());
    TEST_ASSERT_FALSE(r.hold.fired());
    // Rain (never overridden) still fires at once.
    r.reasons = Safety::ReasonRain;
    r.run(STEP_MS, false);
    TEST_ASSERT_TRUE(r.hold.fired());
    TEST_ASSERT_EQUAL(1, r.tally.requests);
}

void test_an_override_after_the_hold_fired_ends_it(void)
{
    HoldRig r;
    r.in.parked = false;
    r.run(61000, false);
    TEST_ASSERT_TRUE(r.hold.fired());
    r.overrideActive = true;
    r.run(STEP_MS, true);
    TEST_ASSERT_FALSE(r.hold.fired());
    TEST_ASSERT_TRUE(r.saw(Event::EndedSafe));
}

// Owner decision (constitution 10.1.0): a failed rain sensor is treated like
// rain, because it may be failing because it is wet. Only those two skip it.
void test_only_rain_and_a_failed_rain_sensor_skip_the_hold(void)
{
    TEST_ASSERT_TRUE(skipsHold(Safety::ReasonRain));
    TEST_ASSERT_TRUE(skipsHold(Safety::ReasonRainSensorFault));
    TEST_ASSERT_TRUE(skipsHold(Safety::ReasonRainSensorFault | Safety::ReasonSourceStale));
    for (uint32_t held :
         {0u,
          (uint32_t)Safety::ReasonWeatherUnsafe,
          (uint32_t)Safety::ReasonSourceStale,
          (uint32_t)Safety::ReasonSourceUnreachable,
          (uint32_t)Safety::ReasonSourceUnreadable,
          (uint32_t)Safety::ReasonSourceOffline,
          (uint32_t)Safety::ReasonNoWeatherSource})
        TEST_ASSERT_FALSE(skipsHold(held));
}

void test_a_failed_rain_sensor_starts_the_close_at_once(void)
{
    HoldRig r;
    Sqmeter q;
    q.reply(r.in.nowMs, true);
    runSqmeter(r, q, 20000, false);
    TEST_ASSERT_EQUAL(0, r.tally.requests);
    const uint64_t before = r.in.nowMs;
    // SQMeter reports its rain sensor failed (reasonFlags bit 10) at its next poll.
    runSqmeter(r, q, 5000, false, false, Safety::SQMETER_RAIN_SENSOR_FAULT);
    TEST_ASSERT_TRUE(r.hold.fired());
    TEST_ASSERT_EQUAL(1, r.tally.requests);
    TEST_ASSERT_UINT64_WITHIN(5000, before, r.tally.firstRequestMs);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    // Its own reason, not rain: the alerts and the UI say "rain sensor fault".
    const Safety::WeatherVerdict v = q.at(r.in.nowMs);
    TEST_ASSERT_EQUAL_UINT32(Safety::ReasonRainSensorFault, v.reasons);
    TEST_ASSERT_NOT_NULL(strstr(Safety::describe(v).c_str(), "rain sensor fault"));
    runSqmeter(r, q, 5000, false, false, Safety::SQMETER_RAIN_SENSOR_FAULT);
    TEST_ASSERT_EQUAL(1, r.tally.closes);
}

void test_rain_from_sqmeter_starts_the_close_at_once(void)
{
    HoldRig r;
    Sqmeter q;
    q.reply(r.in.nowMs, true);
    runSqmeter(r, q, 20000, false);
    const uint64_t before = r.in.nowMs;
    runSqmeter(r, q, 5000, false, false, Safety::SQMETER_RAIN);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
    TEST_ASSERT_UINT64_WITHIN(5000, before, r.tally.firstRequestMs);
}

void test_other_unsafe_weather_from_sqmeter_is_still_held(void)
{
    HoldRig r;
    Sqmeter q;
    q.reply(r.in.nowMs, true);
    runSqmeter(r, q, 20000, false);
    runSqmeter(r, q, 55000, false, false); // cloud cover: held
    TEST_ASSERT_EQUAL(0, r.tally.requests);
    TEST_ASSERT_FALSE(r.hold.fired());
    runSqmeter(r, q, 10000, false, false);
    TEST_ASSERT_EQUAL(1, r.tally.requests);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_hold_zero_passes_the_weather_through);
    RUN_TEST(test_hold_counts_and_reports_what_is_left);
    RUN_TEST(test_hold_starts_fired_after_a_restart);
    RUN_TEST(test_lowering_the_hold_below_the_count_fires_it);
    RUN_TEST(test_policy_carries_the_setting);
    RUN_TEST(test_short_unsafe_spells_never_move_the_roof);
    RUN_TEST(test_a_silent_sqmeter_never_moves_the_roof_within_seconds);
    RUN_TEST(test_a_silent_sqmeter_closes_after_stale_plus_hold);
    RUN_TEST(test_61_s_unsafe_starts_the_close);
    RUN_TEST(test_rain_starts_the_close_at_once);
    RUN_TEST(test_only_rain_and_a_failed_rain_sensor_skip_the_hold);
    RUN_TEST(test_a_failed_rain_sensor_starts_the_close_at_once);
    RUN_TEST(test_rain_from_sqmeter_starts_the_close_at_once);
    RUN_TEST(test_other_unsafe_weather_from_sqmeter_is_still_held);
    RUN_TEST(test_a_flapping_verdict_closes_exactly_once);
    RUN_TEST(test_a_flapping_verdict_reaches_the_hold_in_about_two_minutes);
    RUN_TEST(test_safe_for_the_hold_while_waiting_calls_the_attempt_off);
    RUN_TEST(test_safe_weather_does_not_stop_a_running_close);
    RUN_TEST(test_hold_zero_is_the_old_behaviour);
    RUN_TEST(test_an_override_resets_the_hold);
    RUN_TEST(test_an_override_after_the_hold_fired_ends_it);
    return UNITY_END();
}
