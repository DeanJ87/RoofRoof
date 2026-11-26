// Spec 044 (owner decision): a person's Stop of the automatic close may lapse
// by itself (autoClose.personStopLapse never|rain|always, after
// autoClose.stopHoldMinutes). "never" (the default) is spec 030's stand-down,
// unchanged; "rain" lapses only while it rains (or the rain sensor has failed);
// "always" after any unsafe weather. The close that follows goes through every
// gate: a park request, the park confirmed, then the close.

#include <ArduinoJson.h>

#include "../close_rig/CloseRig.h"
#include "ClosePolicy.h"
#include "HoldLapse.h"
#include "RoofAlerts.h"
#include "RoofStatus.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t HOLD_MS = 5 * 60000; // autoClose.stopHoldMinutes' default
    constexpr uint64_t SIX_HOURS = 6 * 3600000ULL;

    // A rig with `lapse`, unsafe and stood down by a person's stop at T0 + 1000.
    struct LapseRig : Rig
    {
        explicit LapseRig(StopLapse lapse, bool raining)
        {
            Policy p;
            p.stopLapse = lapse;
            seq.setPolicy(p);
            in.raining = raining;
            startUnsafe();
            in.personStops = 1;
            assertEvent(at(T0 + 1000), Event::StoppedByPerson, Cause::None);
            TEST_ASSERT_EQUAL(Phase::HeldByPerson, seq.phase());
        }
    };

    // Steps every second to `untilMs`; the first HoldLapsed event's time, or 0.
    uint64_t lapseTimeUpTo(Rig &r, uint64_t untilMs)
    {
        while (r.in.nowMs + 1000 <= untilMs)
        {
            const Decision d = r.at(r.in.nowMs + 1000);
            if (d.event == Event::HoldLapsed)
                return r.in.nowMs;
        }
        return 0;
    }
} // namespace

// --- "never": spec 030's stand-down, unchanged ---

void test_the_default_is_never_and_five_minutes()
{
    const Policy p;
    TEST_ASSERT_EQUAL(StopLapse::Never, p.stopLapse);
    TEST_ASSERT_EQUAL_UINT64(HOLD_MS, p.stopHoldMs);
    const Config cfg = Config::createDefault();
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Never, cfg.autoClose.personStopLapse);
    TEST_ASSERT_EQUAL_UINT32(5, cfg.autoClose.stopHoldMinutes);
    TEST_ASSERT_EQUAL(StopLapse::Never, policyFrom(cfg).stopLapse);
}

void test_never_holds_through_hours_of_rain()
{
    LapseRig r(StopLapse::Never, true);
    r.in.parked = true;
    Counts c = r.runTo(T0 + SIX_HOURS, 1000);
    assertOnlyReminders(c, Event::StoppedByPerson, 11); // as spec 030: the reminders, nothing else
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    TEST_ASSERT_FALSE(r.seq.standDownLapses());
    TEST_ASSERT_EQUAL_UINT64(NOT_COUNTING, r.seq.standDownLeftMs()); // null while unsafe, as before
}

void test_never_still_rearms_after_the_safe_spell()
{
    LapseRig r(StopLapse::Never, false);
    r.in.weatherSafe = true;
    r.at(T0 + 2000);
    TEST_ASSERT_EQUAL_UINT64(REARM_SAFE_MS, r.seq.standDownLeftMs());
    TEST_ASSERT_FALSE(r.seq.standDownLapses());
    r.runTo(T0 + 2000 + REARM_SAFE_MS - 1000, 1000);
    assertEvent(r.at(T0 + 2000 + REARM_SAFE_MS), Event::EndedSafe, Cause::None);
}

void test_never_alert_text_is_unchanged()
{
    Decision d;
    d.event = Event::StoppedByPerson;
    d.rearmMs = REARM_SAFE_MS;
    const AlertText text = alertFor(d, "");
    TEST_ASSERT_EQUAL_STRING(
        "A person stopped the roof during the automatic close. It stays open while unsafe until someone closes it or the "
        "weather has been safe for 10 minutes.",
        text.message.c_str());
}

// --- "rain" ---

void test_rain_lapses_after_the_hold_of_rain()
{
    LapseRig r(StopLapse::Rain, true);
    r.at(T0 + 2000); // the first held step: the count starts here
    TEST_ASSERT_TRUE(r.seq.standDownLapses());
    TEST_ASSERT_EQUAL_UINT64(HOLD_MS, r.seq.standDownLeftMs());
    r.runTo(T0 + 2000 + HOLD_MS - 1000, 1000);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    TEST_ASSERT_EQUAL_UINT64(1000, r.seq.standDownLeftMs());
    const Decision d = r.at(T0 + 2000 + HOLD_MS);
    assertEvent(d, Event::HoldLapsed, Cause::None);
    TEST_ASSERT_TRUE(d.lapsedInRain);
    TEST_ASSERT_FALSE(d.alert); // not one of the sequence's critical events: the caller raises its own
    TEST_ASSERT_EQUAL(Action::None, d.action);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    TEST_ASSERT_FALSE(r.seq.standDownLapses());
    // The next step starts an attempt: a park request, as for any unsafe spell.
    const Decision next = r.at(T0 + 2100 + HOLD_MS);
    assertEvent(next, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(Action::RequestPark, next.action);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_rain_does_not_lapse_for_other_unsafe_weather()
{
    LapseRig r(StopLapse::Rain, false); // unsafe (cloud, wind, a lost source): no rain
    r.in.parked = true;
    Counts c = r.runTo(T0 + SIX_HOURS, 1000);
    assertOnlyReminders(c, Event::StoppedByPerson, 11);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    TEST_ASSERT_FALSE(r.seq.standDownLapses());
    TEST_ASSERT_EQUAL_UINT64(NOT_COUNTING, r.seq.standDownLeftMs());
}

void test_rain_counts_rain_and_a_failed_rain_sensor_only()
{
    TEST_ASSERT_TRUE(rainForLapse(Safety::ReasonRain));
    TEST_ASSERT_TRUE(rainForLapse(Safety::ReasonRainSensorFault));
    TEST_ASSERT_TRUE(rainForLapse(Safety::ReasonRain | Safety::ReasonSourceStale));
    TEST_ASSERT_FALSE(rainForLapse(Safety::ReasonWeatherUnsafe));
    TEST_ASSERT_FALSE(rainForLapse(Safety::ReasonSourceStale | Safety::ReasonSourceUnreachable));
    TEST_ASSERT_FALSE(rainForLapse(Safety::ReasonNoWeatherSource));
    TEST_ASSERT_FALSE(rainForLapse(0));
}

void test_rain_adds_up_over_short_breaks_and_forgets_after_a_long_one()
{
    {
        LapseRig r(StopLapse::Rain, true);
        r.at(T0 + 2000);
        r.runTo(T0 + 2000 + 3 * 60000, 1000); // 3 min of rain
        r.in.raining = false;
        r.runTo(T0 + 2000 + 4 * 60000, 1000); // 1 min dry but still unsafe: the count waits
        TEST_ASSERT_FALSE(r.seq.standDownLapses());
        TEST_ASSERT_EQUAL_UINT64(NOT_COUNTING, r.seq.standDownLeftMs());
        r.in.raining = true;
        // 2 more minutes of rain make the 5 (the step that turns back counts its second).
        const uint64_t lapsed = lapseTimeUpTo(r, T0 + 2000 + 7 * 60000);
        TEST_ASSERT_NOT_EQUAL(0, lapsed);
        TEST_ASSERT_UINT64_WITHIN(1000, T0 + 2000 + 6 * 60000, lapsed);
    }
    {
        LapseRig r(StopLapse::Rain, true);
        r.at(T0 + 2000);
        r.runTo(T0 + 2000 + 4 * 60000, 1000); // 4 min of rain
        r.in.raining = false;
        r.runTo(T0 + 2000 + 9 * 60000, 1000); // a 5 min break: forgotten
        r.in.raining = true;
        r.runTo(T0 + 2000 + 13 * 60000, 1000); // 4 min more: not 5 since the break
        TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
        TEST_ASSERT_NOT_EQUAL(0, lapseTimeUpTo(r, T0 + 2000 + 15 * 60000));
    }
}

// --- "always" ---

void test_always_lapses_after_the_hold_of_any_unsafe_weather()
{
    LapseRig r(StopLapse::Always, false);
    r.at(T0 + 2000);
    TEST_ASSERT_TRUE(r.seq.standDownLapses());
    const uint64_t lapsed = lapseTimeUpTo(r, T0 + 2000 + 2 * HOLD_MS);
    TEST_ASSERT_EQUAL_UINT64(T0 + 2000 + HOLD_MS, lapsed);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    assertEvent(r.at(lapsed + 100), Event::Started, Cause::None);
}

void test_always_in_rain_says_rain()
{
    LapseRig r(StopLapse::Always, true);
    Decision lapse;
    for (uint64_t t = T0 + 2000; t <= T0 + 2000 + HOLD_MS; t += 1000)
    {
        const Decision d = r.at(t);
        if (d.event == Event::HoldLapsed)
            lapse = d;
    }
    assertEvent(lapse, Event::HoldLapsed, Cause::None);
    TEST_ASSERT_TRUE(lapse.lapsedInRain);
    TEST_ASSERT_EQUAL(StopLapse::Always, lapse.stopLapse);
}

void test_always_counts_the_safe_spell_while_safe()
{
    LapseRig r(StopLapse::Always, false);
    r.at(T0 + 2000);
    r.runTo(T0 + 2000 + 2 * 60000, 1000);
    r.in.weatherSafe = true;
    r.at(T0 + 2000 + 2 * 60000 + 1000);
    // Safe: the countdown is the re-arm spell's again, not the lapse's.
    TEST_ASSERT_FALSE(r.seq.standDownLapses());
    TEST_ASSERT_EQUAL_UINT64(REARM_SAFE_MS, r.seq.standDownLeftMs());
}

// --- What every mode shares ---

void test_a_new_stop_starts_the_count_again()
{
    LapseRig r(StopLapse::Always, true);
    r.at(T0 + 2000);
    r.runTo(T0 + 2000 + 4 * 60000, 1000);
    r.in.personStops = 2; // Stop again: hold it longer
    const Decision d = r.at(T0 + 2000 + 4 * 60000 + 1000);
    TEST_ASSERT_EQUAL(Event::None, d.event);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
    TEST_ASSERT_FALSE(r.seq.standDownLapses()); // the count starts on the next step
    r.at(T0 + 2000 + 4 * 60000 + 2000);
    TEST_ASSERT_EQUAL_UINT64(HOLD_MS, r.seq.standDownLeftMs());
    const uint64_t lapsed = lapseTimeUpTo(r, T0 + 2000 + 12 * 60000);
    TEST_ASSERT_EQUAL_UINT64(T0 + 2000 + 4 * 60000 + 2000 + HOLD_MS, lapsed);
}

void test_the_close_after_a_lapse_waits_for_the_park()
{
    LapseRig r(StopLapse::Rain, true);
    r.in.parked = false;
    const uint64_t lapsed = lapseTimeUpTo(r, T0 + 2000 + 2 * HOLD_MS);
    TEST_ASSERT_NOT_EQUAL(0, lapsed);
    Counts c = r.runTo(lapsed + 60000, 100);
    TEST_ASSERT_EQUAL(0, c.closes); // no park, no close
    TEST_ASSERT_TRUE(c.requests >= 1);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
    r.in.parked = true;
    r.at(r.in.nowMs + 100);
    // The close goes out once the park has held for the park hold, not before.
    const uint64_t parkedAt = r.in.nowMs;
    uint64_t closeAt = 0;
    while (closeAt == 0 && r.in.nowMs < parkedAt + 2 * PARK_SETTLE_MS)
        if (r.at(r.in.nowMs + 100).action == Action::Close)
            closeAt = r.in.nowMs;
    TEST_ASSERT_EQUAL_UINT64(parkedAt + PARK_SETTLE_MS, closeAt);
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    // A person's Stop of that close stands it down again, with its alert.
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.roof = State::Closing;
    r.at(r.in.nowMs + 100);
    r.in.personStops = 5;
    r.in.roof = State::Stopped;
    assertEvent(r.at(r.in.nowMs + 100), Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_a_halted_close_never_lapses()
{
    Rig r;
    Policy p;
    p.stopLapse = StopLapse::Always;
    r.seq.setPolicy(p);
    r.in.raining = true;
    r.startAndClose();
    const Decision d = r.at(T0 + 4100 + 60000 + CLOSE_GUARD_SLACK_MS);
    assertEvent(d, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
    r.in.roof = State::Stopped;
    Counts c = r.runTo(r.in.nowMs + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
    TEST_ASSERT_FALSE(r.seq.standDownLapses());
}

void test_a_daylight_stop_with_the_weather_safe_never_lapses()
{
    Rig r;
    Policy p;
    p.stopLapse = StopLapse::Always;
    r.seq.setPolicy(p);
    r.in.daylight = true;
    r.at(T0); // daylight wants the roof closed, the weather safe
    r.in.personStops = 1;
    assertEvent(r.at(T0 + 1000), Event::StoppedByPerson, Cause::None);
    Counts c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests + c.closes);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
}

void test_the_closed_roof_ends_the_hold_before_any_lapse()
{
    LapseRig r(StopLapse::Always, true);
    r.runTo(T0 + 2000 + HOLD_MS - 2000, 1000);
    r.in.roof = State::Closed;
    assertEvent(r.at(r.in.nowMs + 1000), Event::Closed, Cause::None);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    TEST_ASSERT_EQUAL(0, r.runTo(T0 + 2 * HOLD_MS, 1000).events.size());
}

// --- HoldLapse alone ---

void test_hold_lapse_caps_a_late_step()
{
    HoldLapse lapse;
    HoldLapse::Step s;
    s.held = true;
    s.counts = true;
    s.lapseMs = 120000;
    s.nowMs = 1000;
    TEST_ASSERT_FALSE(lapse.update(s));
    s.nowMs = 1000 + 600000; // ten minutes late: counts one second
    TEST_ASSERT_FALSE(lapse.update(s));
    TEST_ASSERT_EQUAL_UINT64(119000, lapse.leftMs());
    s.lapseMs = 0; // "never"
    s.nowMs += 1000;
    TEST_ASSERT_FALSE(lapse.update(s));
    TEST_ASSERT_EQUAL_UINT64(HoldLapse::NOT_LAPSING, lapse.leftMs());
}

// --- The setting ---

void test_policy_from_config_and_its_range()
{
    Config cfg = Config::createDefault();
    cfg.autoClose.personStopLapse = AutoCloseConfig::PersonStopLapse::Rain;
    cfg.autoClose.stopHoldMinutes = 12;
    Policy p = policyFrom(cfg);
    TEST_ASSERT_EQUAL(StopLapse::Rain, p.stopLapse);
    TEST_ASSERT_EQUAL_UINT64(12 * 60000, p.stopHoldMs);
    cfg.autoClose.personStopLapse = AutoCloseConfig::PersonStopLapse::Always;
    TEST_ASSERT_EQUAL(StopLapse::Always, policyFrom(cfg).stopLapse);
    p.stopHoldMs = 1000;
    TEST_ASSERT_EQUAL_UINT64(STOP_HOLD_RANGE.minMs, clamp(p).stopHoldMs);
    p.stopHoldMs = 24 * 3600000ULL;
    TEST_ASSERT_EQUAL_UINT64(STOP_HOLD_RANGE.maxMs, clamp(p).stopHoldMs);
}

void test_config_json_in_and_out()
{
    Config cfg = Config::createDefault();
    std::string reason;
    TEST_ASSERT_TRUE_MESSAGE(
        Config::applyJson("{\"autoClose\":{\"personStopLapse\":\"rain\",\"stopHoldMinutes\":7}}", cfg, false, &reason), reason.c_str());
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Rain, cfg.autoClose.personStopLapse);
    TEST_ASSERT_EQUAL_UINT32(7, cfg.autoClose.stopHoldMinutes);
    const std::string json = cfg.toJson(false);
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"personStopLapse\":\"rain\""));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"stopHoldMinutes\":7"));
    const auto again = Config::fromJson(json);
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Rain, again->autoClose.personStopLapse);
    TEST_ASSERT_TRUE(Config::applyJson("{\"autoClose\":{\"personStopLapse\":\"always\"}}", cfg, false, &reason));
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Always, cfg.autoClose.personStopLapse);
    TEST_ASSERT_TRUE(Config::applyJson("{\"autoClose\":{\"personStopLapse\":\"never\"}}", cfg, false, &reason));
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Never, cfg.autoClose.personStopLapse);
    // Stored settings from before spec 044: never, 5 minutes.
    Config old = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson("{\"autoClose\":{\"remindMinutes\":30}}", old, false, &reason));
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Never, old.autoClose.personStopLapse);
    TEST_ASSERT_EQUAL_UINT32(5, old.autoClose.stopHoldMinutes);
}

void test_config_refuses_a_bad_value()
{
    std::string reason;
    Config cfg = Config::createDefault();
    // A save (the UI and API: strict) is refused.
    TEST_ASSERT_FALSE(Config::applyJson("{\"autoClose\":{\"personStopLapse\":\"sometimes\"}}", cfg, true, &reason));
    TEST_ASSERT_EQUAL_STRING("Automatic close: personStopLapse must be never, rain or always", reason.c_str());
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Never, cfg.autoClose.personStopLapse);
    TEST_ASSERT_FALSE(Config::applyJson("{\"autoClose\":{\"personStopLapse\":true}}", cfg, true, &reason));
    // Stored by a later release with a name this one doesn't know: never, and the store still loads.
    Config stored = Config::createDefault();
    stored.autoClose.personStopLapse = AutoCloseConfig::PersonStopLapse::Always;
    TEST_ASSERT_TRUE_MESSAGE(
        Config::applyJson("{\"autoClose\":{\"personStopLapse\":\"later\",\"remindMinutes\":40}}", stored, false, &reason), reason.c_str());
    TEST_ASSERT_EQUAL(AutoCloseConfig::PersonStopLapse::Never, stored.autoClose.personStopLapse);
    TEST_ASSERT_EQUAL_UINT32(40, stored.autoClose.remindMinutes);
    for (const char *bad : {"1", "61", "-1", "2.5"})
    {
        Config other = Config::createDefault();
        const std::string json = std::string("{\"autoClose\":{\"stopHoldMinutes\":") + bad + "}}";
        TEST_ASSERT_FALSE_MESSAGE(Config::applyJson(json, other, false, &reason), bad);
    }
    Config edge = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson("{\"autoClose\":{\"stopHoldMinutes\":2}}", edge, false, &reason));
    TEST_ASSERT_TRUE(Config::applyJson("{\"autoClose\":{\"stopHoldMinutes\":60}}", edge, false, &reason));
}

// --- Wording and status ---

void test_the_stop_alert_says_when_it_lapses()
{
    Decision d;
    d.event = Event::StoppedByPerson;
    d.rearmMs = REARM_SAFE_MS;
    d.stopLapse = StopLapse::Rain;
    d.stopHoldMs = HOLD_MS;
    std::string message = alertFor(d, "").message;
    TEST_ASSERT_NOT_NULL(std::strstr(message.c_str(), "After 5 minutes of rain the hold lapses and the close goes ahead by itself"));
    d.stopLapse = StopLapse::Always;
    message = alertFor(d, "").message;
    TEST_ASSERT_NOT_NULL(std::strstr(message.c_str(), "After 5 minutes of unsafe weather the hold lapses"));
    d.daylight = true; // a daylight close's stop never lapses
    message = alertFor(d, "").message;
    TEST_ASSERT_NULL(std::strstr(message.c_str(), "lapses"));
}

void test_the_lapse_alert()
{
    Decision d;
    d.event = Event::HoldLapsed;
    d.lapsedInRain = true;
    d.stopLapse = StopLapse::Rain;
    d.stopHoldMs = HOLD_MS;
    const Alerts::Alert alert = holdLapsedAlert(d, "Rain");
    TEST_ASSERT_EQUAL_STRING("Hold lapsed: closing because it's still raining", alert.title.c_str());
    TEST_ASSERT_EQUAL(Alerts::AlertType::RoofNotClosed, alert.type);
    TEST_ASSERT_TRUE(Alerts::isCritical(alert.type));
    TEST_ASSERT_NOT_NULL(std::strstr(alert.message.c_str(), "after 5 minutes of rain the hold lapsed"));
    TEST_ASSERT_NOT_NULL(std::strstr(alert.message.c_str(), "Weather: Rain"));
    d.lapsedInRain = false;
    d.stopLapse = StopLapse::Always;
    TEST_ASSERT_EQUAL_STRING("Hold lapsed: closing because it's still unsafe", holdLapsedAlert(d, "").title.c_str());
    TEST_ASSERT_EQUAL_STRING("holdLapsed", eventName(Event::HoldLapsed));
    TEST_ASSERT_FALSE(isCritical(Event::HoldLapsed));
}

void test_the_status_shows_the_lapse_countdown()
{
    RoofIo::Snapshot s;
    s.closeRearmSeconds = 240;
    s.closeRearmLapse = true;
    DynamicJsonDocument doc(2048);
    RoofIo::writeCloseRearm(doc.to<JsonObject>(), s);
    TEST_ASSERT_EQUAL_INT(240, doc["closeRearmSeconds"].as<int>());
    TEST_ASSERT_TRUE(doc["closeRearmLapse"].as<bool>());
    s.closeRearmSeconds = -1; // not counting: never a lapse
    DynamicJsonDocument none(2048);
    RoofIo::writeCloseRearm(none.to<JsonObject>(), s);
    TEST_ASSERT_TRUE(none["closeRearmSeconds"].isNull());
    TEST_ASSERT_FALSE(none["closeRearmLapse"].as<bool>());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_default_is_never_and_five_minutes);
    RUN_TEST(test_never_holds_through_hours_of_rain);
    RUN_TEST(test_never_still_rearms_after_the_safe_spell);
    RUN_TEST(test_never_alert_text_is_unchanged);
    RUN_TEST(test_rain_lapses_after_the_hold_of_rain);
    RUN_TEST(test_rain_does_not_lapse_for_other_unsafe_weather);
    RUN_TEST(test_rain_counts_rain_and_a_failed_rain_sensor_only);
    RUN_TEST(test_rain_adds_up_over_short_breaks_and_forgets_after_a_long_one);
    RUN_TEST(test_always_lapses_after_the_hold_of_any_unsafe_weather);
    RUN_TEST(test_always_in_rain_says_rain);
    RUN_TEST(test_always_counts_the_safe_spell_while_safe);
    RUN_TEST(test_a_new_stop_starts_the_count_again);
    RUN_TEST(test_the_close_after_a_lapse_waits_for_the_park);
    RUN_TEST(test_a_halted_close_never_lapses);
    RUN_TEST(test_a_daylight_stop_with_the_weather_safe_never_lapses);
    RUN_TEST(test_the_closed_roof_ends_the_hold_before_any_lapse);
    RUN_TEST(test_hold_lapse_caps_a_late_step);
    RUN_TEST(test_policy_from_config_and_its_range);
    RUN_TEST(test_config_json_in_and_out);
    RUN_TEST(test_config_refuses_a_bad_value);
    RUN_TEST(test_the_stop_alert_says_when_it_lapses);
    RUN_TEST(test_the_lapse_alert);
    RUN_TEST(test_the_status_shows_the_lapse_countdown);
    return UNITY_END();
}
