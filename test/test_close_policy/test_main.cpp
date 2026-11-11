// Spec 014: the automatic close's policy as settings. The defaults are the
// constants specs 005 and 013 chose (a default config behaves as before), every
// value is clamped to its range, and each setting is used at both ends of its
// range; the critical alert resend cap. The remote stop rule, the MQTT change
// gap and the daylight band are in test_policy_inputs.

#include <unity.h>

#include "../close_rig/CloseRig.h"
#include "AlertRetry.h"
#include "ClosePolicy.h"
#include "PlatformHold.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Policy with(void (*change)(Policy &))
    {
        Policy p;
        change(p);
        return p;
    }

    void assertSame(const Policy &a, const Policy &b)
    {
        TEST_ASSERT_EQUAL_UINT64(a.parkRequestGapMs, b.parkRequestGapMs);
        TEST_ASSERT_EQUAL_UINT64(a.parkSettleMs, b.parkSettleMs);
        TEST_ASSERT_EQUAL_UINT64(a.retryAfterMs, b.retryAfterMs);
        TEST_ASSERT_EQUAL_UINT64(a.rearmSafeMs, b.rearmSafeMs);
        TEST_ASSERT_EQUAL_UINT64(a.closeStartMs, b.closeStartMs);
        TEST_ASSERT_EQUAL_UINT64(a.closeGuardSlackMs, b.closeGuardSlackMs);
        TEST_ASSERT_EQUAL_UINT64(a.remindMs, b.remindMs);
        TEST_ASSERT_EQUAL(a.retryNotStarted, b.retryNotStarted);
    }

    // A rig whose sequence runs `policy` from its first unsafe step.
    struct PolicyRig : Rig
    {
        explicit PolicyRig(const Policy &policy) { seq.setPolicy(policy); }
    };

    // Unsafe at T0, the park confirmed from T0 + 100: the step the close goes out.
    uint64_t closeTimeFor(const Policy &policy)
    {
        PolicyRig r(policy);
        r.in.parked = false;
        r.startUnsafe();
        r.in.parked = true;
        r.at(T0 + 100);
        for (uint64_t t = T0 + 200; t < T0 + 60000; t += 100)
            if (r.at(t).action == Action::Close)
                return t;
        return 0;
    }
} // namespace

// --- Defaults: today's values ---

void test_defaults_are_the_constants_of_specs_005_and_013(void)
{
    const Policy p;
    TEST_ASSERT_EQUAL_UINT64(30000, p.parkRequestGapMs);
    TEST_ASSERT_EQUAL_UINT64(4000, p.parkSettleMs);
    TEST_ASSERT_EQUAL_UINT64(300000, p.retryAfterMs);
    TEST_ASSERT_EQUAL_UINT64(600000, p.rearmSafeMs);
    TEST_ASSERT_EQUAL_UINT64(30000, p.closeStartMs);
    TEST_ASSERT_EQUAL_UINT64(30000, p.closeGuardSlackMs);
    TEST_ASSERT_EQUAL_UINT64(1800000, p.remindMs);
    TEST_ASSERT_FALSE(p.retryNotStarted);
    // A new sequence runs the defaults, and they are inside their ranges.
    Sequence seq;
    const Policy &now = seq.currentPolicy();
    const Policy clamped = clamp(p);
    assertSame(p, now);
    assertSame(p, clamped);
}

void test_default_config_gives_the_default_policy(void)
{
    const Config cfg = Config::createDefault();
    const Policy from = policyFrom(cfg);
    const Policy p;
    assertSame(p, from);
    TEST_ASSERT_FALSE(cfg.autoClose.remoteStop);
    TEST_ASSERT_FALSE(cfg.ui.confirmOpenClose);
    TEST_ASSERT_EQUAL_UINT32(1, cfg.mqtt.changeMinSeconds);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, cfg.daylight.bandDeg);
    TEST_ASSERT_FALSE(cfg.alpaca.enabled); // spec 014 keeps Alpaca off by default
}

void test_config_without_the_new_settings_loads_the_defaults(void)
{
    // A document saved before spec 014: none of its sections name the new fields.
    Config cfg = Config::createDefault();
    cfg.autoClose.rearmMinutes = 60;
    cfg.autoClose.remoteStop = true;
    TEST_ASSERT_TRUE(Config::applyJson("{\"daylight\":{\"mode\":\"alert\"},\"mqtt\":{\"enabled\":false}}", cfg, false));
    TEST_ASSERT_EQUAL_UINT32(60, cfg.autoClose.rearmMinutes); // untouched: applyJson only changes what it names
    const auto fresh = Config::fromJson("{\"deviceName\":\"Old\"}");
    TEST_ASSERT_TRUE(fresh.has_value());
    const Policy from = policyFrom(*fresh);
    const Policy p;
    assertSame(p, from);
    TEST_ASSERT_FALSE(fresh->autoClose.remoteStop);
}

void test_settings_round_trip_and_bad_choice_is_refused(void)
{
    Config cfg = Config::createDefault();
    cfg.autoClose.notStarted = AutoCloseConfig::NotStarted::Retry;
    cfg.autoClose.remindMinutes = 120;
    cfg.autoClose.remoteStop = true;
    cfg.ui.confirmOpenClose = true;
    cfg.mqtt.changeMinSeconds = 60;
    cfg.daylight.bandDeg = 2.0f;
    const auto again = Config::fromJson(cfg.toJson());
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_TRUE(again->autoClose.notStarted == AutoCloseConfig::NotStarted::Retry);
    TEST_ASSERT_EQUAL_UINT32(120, again->autoClose.remindMinutes);
    TEST_ASSERT_TRUE(again->autoClose.remoteStop);
    TEST_ASSERT_TRUE(again->ui.confirmOpenClose);
    TEST_ASSERT_EQUAL_UINT32(60, again->mqtt.changeMinSeconds);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, again->daylight.bandDeg);
    std::string error;
    Config bad = Config::createDefault();
    TEST_ASSERT_FALSE(Config::applyJson("{\"autoClose\":{\"notStarted\":\"never\"}}", bad, true, &error));
    TEST_ASSERT_EQUAL_STRING("Automatic close: notStarted must be halt or retry", error.c_str());
}

// --- Clamping: the logic never runs a value outside its range ---

void test_clamp_holds_every_value_to_its_range(void)
{
    Policy low;
    low.parkRequestGapMs = 0;
    low.parkSettleMs = 0;
    low.retryAfterMs = 0;
    low.rearmSafeMs = 0;
    low.closeStartMs = 0;
    low.closeGuardSlackMs = 0;
    low.remindMs = 0;
    const Policy a = clamp(low);
    TEST_ASSERT_EQUAL_UINT64(10000, a.parkRequestGapMs);
    TEST_ASSERT_EQUAL_UINT64(4000, a.parkSettleMs);
    TEST_ASSERT_EQUAL_UINT64(60000, a.retryAfterMs);
    TEST_ASSERT_EQUAL_UINT64(300000, a.rearmSafeMs);
    TEST_ASSERT_EQUAL_UINT64(25000, a.closeStartMs);
    TEST_ASSERT_EQUAL_UINT64(10000, a.closeGuardSlackMs);
    TEST_ASSERT_EQUAL_UINT64(600000, a.remindMs);
    Policy high;
    high.parkRequestGapMs = UINT64_MAX;
    high.parkSettleMs = UINT64_MAX;
    high.retryAfterMs = UINT64_MAX;
    high.rearmSafeMs = UINT64_MAX;
    high.closeStartMs = UINT64_MAX;
    high.closeGuardSlackMs = UINT64_MAX;
    high.remindMs = UINT64_MAX;
    const Policy b = clamp(high);
    TEST_ASSERT_EQUAL_UINT64(120000, b.parkRequestGapMs);
    TEST_ASSERT_EQUAL_UINT64(30000, b.parkSettleMs);
    TEST_ASSERT_EQUAL_UINT64(1800000, b.retryAfterMs);
    TEST_ASSERT_EQUAL_UINT64(3600000, b.rearmSafeMs);
    TEST_ASSERT_EQUAL_UINT64(120000, b.closeStartMs);
    TEST_ASSERT_EQUAL_UINT64(120000, b.closeGuardSlackMs);
    TEST_ASSERT_EQUAL_UINT64(7200000, b.remindMs);
    // The close-start floor outlasts spec 010's network hold of a close.
    TEST_ASSERT_TRUE(RoofIo::NETWORK_HOLD_MAX_MS + 5000 < CLOSE_START_RANGE.minMs); // spec 010's margin
}

void test_config_limits_match_the_logic_ranges(void)
{
    Config cfg = Config::createDefault();
    AutoCloseConfig &a = cfg.autoClose;
    a.rearmMinutes = AutoCloseConfig::MIN_REARM_MINUTES;
    a.parkRequestSeconds = AutoCloseConfig::MIN_PARK_REQUEST_SECONDS;
    a.parkHoldSeconds = AutoCloseConfig::MIN_PARK_HOLD_SECONDS;
    a.retryMinutes = AutoCloseConfig::MIN_RETRY_MINUTES;
    a.closeStartSeconds = AutoCloseConfig::MIN_CLOSE_START_SECONDS;
    a.closeGuardSeconds = AutoCloseConfig::MIN_CLOSE_GUARD_SECONDS;
    a.remindMinutes = AutoCloseConfig::MIN_REMIND_MINUTES;
    TEST_ASSERT_TRUE(cfg.validate());
    Policy p = policyFrom(cfg);
    TEST_ASSERT_EQUAL_UINT64(REARM_SAFE_RANGE.minMs, p.rearmSafeMs);
    TEST_ASSERT_EQUAL_UINT64(PARK_REQUEST_GAP_RANGE.minMs, p.parkRequestGapMs);
    TEST_ASSERT_EQUAL_UINT64(PARK_SETTLE_RANGE.minMs, p.parkSettleMs);
    TEST_ASSERT_EQUAL_UINT64(RETRY_AFTER_RANGE.minMs, p.retryAfterMs);
    TEST_ASSERT_EQUAL_UINT64(CLOSE_START_RANGE.minMs, p.closeStartMs);
    TEST_ASSERT_EQUAL_UINT64(CLOSE_GUARD_SLACK_RANGE.minMs, p.closeGuardSlackMs);
    TEST_ASSERT_EQUAL_UINT64(REMIND_RANGE.minMs, p.remindMs);
    a.rearmMinutes = AutoCloseConfig::MAX_REARM_MINUTES;
    a.parkRequestSeconds = AutoCloseConfig::MAX_PARK_REQUEST_SECONDS;
    a.parkHoldSeconds = AutoCloseConfig::MAX_PARK_HOLD_SECONDS;
    a.retryMinutes = AutoCloseConfig::MAX_RETRY_MINUTES;
    a.closeStartSeconds = AutoCloseConfig::MAX_CLOSE_START_SECONDS;
    a.closeGuardSeconds = AutoCloseConfig::MAX_CLOSE_GUARD_SECONDS;
    a.remindMinutes = AutoCloseConfig::MAX_REMIND_MINUTES;
    TEST_ASSERT_TRUE(cfg.validate());
    p = policyFrom(cfg);
    TEST_ASSERT_EQUAL_UINT64(REARM_SAFE_RANGE.maxMs, p.rearmSafeMs);
    TEST_ASSERT_EQUAL_UINT64(PARK_REQUEST_GAP_RANGE.maxMs, p.parkRequestGapMs);
    TEST_ASSERT_EQUAL_UINT64(PARK_SETTLE_RANGE.maxMs, p.parkSettleMs);
    TEST_ASSERT_EQUAL_UINT64(RETRY_AFTER_RANGE.maxMs, p.retryAfterMs);
    TEST_ASSERT_EQUAL_UINT64(CLOSE_START_RANGE.maxMs, p.closeStartMs);
    TEST_ASSERT_EQUAL_UINT64(CLOSE_GUARD_SLACK_RANGE.maxMs, p.closeGuardSlackMs);
    TEST_ASSERT_EQUAL_UINT64(REMIND_RANGE.maxMs, p.remindMs);
    // The longest values still fit the stored document.
    TEST_ASSERT_TRUE(cfg.toJson(false, false).size() < Config::MAX_PERSISTED_JSON_BYTES);
}

// --- Each setting at its limits ---

// Constitution II: the close only after the park has held for the hold time.
void test_park_hold_limits(void)
{
    TEST_ASSERT_EQUAL_UINT64(T0 + 100 + 4000, closeTimeFor(Policy()));
    TEST_ASSERT_EQUAL_UINT64(T0 + 100 + 4000, closeTimeFor(with([](Policy &p) { p.parkSettleMs = 4000; })));
    TEST_ASSERT_EQUAL_UINT64(T0 + 100 + 30000, closeTimeFor(with([](Policy &p) { p.parkSettleMs = 30000; })));
    // Never shorter than spec 005's 4 s (two N.I.N.A. polls): 0 and 2 s are clamped to it.
    TEST_ASSERT_EQUAL_UINT64(T0 + 100 + 4000, closeTimeFor(with([](Policy &p) { p.parkSettleMs = 0; })));
    TEST_ASSERT_EQUAL_UINT64(T0 + 100 + 4000, closeTimeFor(with([](Policy &p) { p.parkSettleMs = 2000; })));
}

void test_park_request_spacing_limits(void)
{
    for (const uint64_t gap : {10000ULL, 120000ULL})
    {
        Policy p;
        p.parkRequestGapMs = gap;
        PolicyRig r(p);
        TEST_ASSERT_EQUAL(Action::RequestPark, r.startUnsafe().action);
        TEST_ASSERT_EQUAL(Action::None, r.at(T0 + gap - 1).action);
        TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + gap).action);
    }
}

void test_back_off_limits(void)
{
    for (const uint64_t back : {60000ULL, 1800000ULL})
    {
        Policy p;
        p.retryAfterMs = back;
        PolicyRig r(p);
        r.startUnsafe();
        const Decision failed = r.at(T0 + DEFAULT_PARK_TIMEOUT_MS);
        assertEvent(failed, Event::ParkNotConfirmed, Cause::ParkTimeout);
        TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
        TEST_ASSERT_EQUAL(Action::None, r.at(T0 + DEFAULT_PARK_TIMEOUT_MS + back - 1).action);
        TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + DEFAULT_PARK_TIMEOUT_MS + back).action);
    }
}

void test_rearm_spell_limits(void)
{
    for (const uint64_t spell : {300000ULL, 3600000ULL})
    {
        Policy p;
        p.rearmSafeMs = spell;
        PolicyRig r(p);
        r.startAndClose();
        r.in.personStops = 1;
        r.in.roof = State::Stopped;
        const Decision stopped = r.at(T0 + 10000);
        assertEvent(stopped, Event::StoppedByPerson, Cause::None);
        TEST_ASSERT_TRUE(stopped.alert);
        TEST_ASSERT_EQUAL_UINT64(spell, stopped.rearmMs);
        r.in.weatherSafe = true;
        r.at(T0 + 20000);
        r.at(T0 + 20000 + spell - 1);
        TEST_ASSERT_EQUAL(Phase::HeldByPerson, r.seq.phase());
        assertEvent(r.at(T0 + 20000 + spell), Event::EndedSafe, Cause::None);
        TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    }
}

void test_close_guard_limits(void)
{
    for (const uint64_t slack : {10000ULL, 120000ULL})
    {
        Policy p;
        p.closeGuardSlackMs = slack;
        PolicyRig r(p);
        r.startAndClose(); // commanded at T0 + 4100, the move's timeout 60 s
        r.at(T0 + 5000);
        TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 4100 + 60000 + slack - 1).action);
        const Decision halted = r.at(T0 + 4100 + 60000 + slack);
        assertEvent(halted, Event::CloseFailed, Cause::TooLong);
        TEST_ASSERT_EQUAL(Action::Stop, halted.action);
        TEST_ASSERT_EQUAL_UINT64(60000 + slack, halted.limitMs);
        TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
    }
}

namespace
{
    // The close commanded at T0 + 4100 waits (spec 003 closePending) and never starts.
    void waitForever(Rig &r)
    {
        r.in.weatherSafe = false;
        r.in.parked = true;
        r.at(T0);
        r.at(T0 + 100);
        TEST_ASSERT_EQUAL(Action::Close, r.at(T0 + 4100).action);
        r.seq.closeAnswered(CloseAnswer::Started);
        r.in.closePending = true;
        r.at(T0 + 5000);
    }
} // namespace

void test_not_started_halts_by_default(void)
{
    Rig r;
    waitForever(r);
    TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 4100 + 30000 - 1).action);
    const Decision d = r.at(T0 + 4100 + 30000);
    assertEvent(d, Event::CloseFailed, Cause::TooLong);
    TEST_ASSERT_EQUAL(Action::Stop, d.action);
    TEST_ASSERT_EQUAL(Phase::Halted, r.seq.phase());
}

// Spec 014: retry calls the waiting close off and backs off as for a refused
// close; the next close needs the park confirmed again.
void test_not_started_retry_backs_off(void)
{
    for (const uint64_t start : {25000ULL, 120000ULL})
    {
        Policy p;
        p.retryNotStarted = true;
        p.closeStartMs = start;
        PolicyRig r(p);
        waitForever(r);
        TEST_ASSERT_EQUAL(Action::None, r.at(T0 + 4100 + start - 1).action);
        const Decision d = r.at(T0 + 4100 + start);
        assertEvent(d, Event::CloseFailed, Cause::NotStarted);
        TEST_ASSERT_TRUE(d.alert);
        TEST_ASSERT_EQUAL(Action::Stop, d.action); // the waiting close is called off
        TEST_ASSERT_EQUAL_UINT64(start, d.limitMs);
        TEST_ASSERT_EQUAL(Phase::RetryWait, r.seq.phase());
        r.in.closePending = false; // what the stop does
        const uint64_t at = T0 + 4100 + start;
        Counts c = r.runTo(at + RETRY_AFTER_MS - 100, 100);
        TEST_ASSERT_EQUAL(0, c.closes);
        TEST_ASSERT_EQUAL(Action::Close, r.at(at + RETRY_AFTER_MS).action);
        const AlertText text = alertFor(d, "open");
        TEST_ASSERT_EQUAL_STRING("Roof didn't close", text.title.c_str());
        const std::string expected = "The close didn't start within " + std::to_string(start / 1000) +
                                     " s and was called off; it tries again in 5 minutes. Roof: open";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), text.message.c_str());
    }
}

void test_not_started_retry_survives_a_restart(void)
{
    PolicyRig r(with([](Policy &p) { p.retryNotStarted = true; }));
    waitForever(r);
    r.at(T0 + 4100 + 30000);
    const Sequence::Saved saved = r.seq.saved();
    TEST_ASSERT_EQUAL(Phase::RetryWait, saved.phase);
    TEST_ASSERT_EQUAL(Cause::NotStarted, saved.cause);
    Sequence again;
    again.restore(saved);
    r.in.closePending = false;
    r.in.nowMs = T0 + 40000;
    again.step(r.in);
    TEST_ASSERT_EQUAL(Phase::RetryWait, again.phase());
}

void test_reminder_limits(void)
{
    for (const uint64_t remind : {600000ULL, 7200000ULL})
    {
        Policy p;
        p.remindMs = remind;
        PolicyRig r(p);
        r.startAndClose();
        r.in.personStops = 1;
        r.in.roof = State::Stopped;
        TEST_ASSERT_TRUE(r.at(T0 + 10000).alert);
        Counts c = r.runTo(T0 + 6 * 3600000ULL, 1000);
        // The first alert went out at once; then one every interval while the roof stays open.
        assertOnlyReminders(c, Event::StoppedByPerson, static_cast<size_t>((6 * 3600000ULL - 10000) / remind));
    }
}

// A long reminder interval spaces the reminders, never a new episode's first
// alert: the throttle stays at most spec 005's 30 minutes.
void test_long_reminder_keeps_the_30_minute_throttle(void)
{
    Policy p;
    p.remindMs = 7200000;
    p.rearmSafeMs = 300000;
    PolicyRig r(p);
    r.startAndClose();
    r.in.personStops = 1;
    r.in.roof = State::Stopped;
    TEST_ASSERT_TRUE(r.at(T0 + 10000).alert);
    // Safe for the re-arm spell: the stand-down ends.
    r.in.weatherSafe = true;
    r.at(T0 + 20000);
    assertEvent(r.at(T0 + 20000 + 300000), Event::EndedSafe, Cause::None);
    // Unsafe again 31 minutes after the first alert, a new close, a new stop: it alerts at once.
    const uint64_t again = T0 + 10000 + 1860000;
    r.startAndCloseFrom(again);
    r.in.personStops = 2;
    r.in.roof = State::Stopped;
    const Decision d = r.at(again + 10000);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(d.alert);
}

// A stand-down the throttle held back is raised within 30 minutes of the last
// send, however long the reminder interval (review: never a silent stand-down).
void test_throttled_stand_down_alerts_within_30_minutes(void)
{
    Policy p;
    p.remindMs = 7200000;
    p.rearmSafeMs = 300000;
    PolicyRig r(p);
    r.startAndClose();
    r.in.personStops = 1;
    r.in.roof = State::Stopped;
    TEST_ASSERT_TRUE(r.at(T0 + 10000).alert); // the first stand-down: at once
    r.in.weatherSafe = true;
    r.at(T0 + 20000);
    assertEvent(r.at(T0 + 320000), Event::EndedSafe, Cause::None);
    // Eleven minutes after the first alert: unsafe, a new close, a new stop.
    const uint64_t again = T0 + 10000 + 660000;
    r.startAndCloseFrom(again);
    r.in.personStops = 2;
    r.in.roof = State::Stopped;
    const Decision held = r.at(again + 10000);
    assertEvent(held, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_FALSE(held.alert); // throttled: the same event alerted 11 minutes ago
    // It goes out 30 minutes after the first send, not 120.
    Counts c = r.runTo(T0 + 10000 + REMIND_MS - 1000, 1000);
    TEST_ASSERT_EQUAL(0, c.alerts);
    const Decision sent = r.at(T0 + 10000 + REMIND_MS);
    assertEvent(sent, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(sent.alert);
    // After that, the reminders follow the set interval.
    Counts later = r.runTo(T0 + 10000 + REMIND_MS + 7200000 - 1000, 1000);
    TEST_ASSERT_EQUAL(0, later.alerts);
    TEST_ASSERT_TRUE(r.at(T0 + 10000 + REMIND_MS + 7200000).alert);
}

// The stand-down's alert says who stopped the close (spec 014 remote stop).
void test_remote_stop_is_named_in_the_alert(void)
{
    Rig r;
    r.startAndClose();
    r.in.personStops = 1;
    r.in.remoteStops = 1; // RoofControl counted an honoured remote stop
    r.in.roof = State::Stopped;
    const Decision d = r.at(T0 + 10000);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_TRUE(d.remoteStop);
    TEST_ASSERT_EQUAL_STRING(
        "An imaging app or MQTT stopped the roof during the automatic close. It stays open while unsafe until someone closes it or "
        "the weather has been safe for 10 minutes.",
        alertFor(d, "").message.c_str());
    // Its reminders say so too.
    const Decision reminder = r.at(T0 + 10000 + REMIND_MS);
    TEST_ASSERT_TRUE(reminder.reminder);
    TEST_ASSERT_TRUE(reminder.remoteStop);
    // A person's stop later is a person's again.
    r.in.roof = State::Closed;
    r.at(T0 + 10000 + REMIND_MS + 1000);
    r.startAndCloseFrom(T0 + 10000 + REMIND_MS + 2000);
    r.in.personStops = 2;
    r.in.roof = State::Stopped;
    const Decision person = r.at(T0 + 10000 + REMIND_MS + 20000);
    assertEvent(person, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_FALSE(person.remoteStop);
}

void test_wording_names_the_set_times(void)
{
    Decision d;
    d.event = Event::CloseFailed;
    d.cause = Cause::Refused;
    TEST_ASSERT_EQUAL_STRING("The roof refused to close; it tries again in 5 minutes. Refused: x", alertFor(d, "x").message.c_str());
    d.retryMs = 60000;
    TEST_ASSERT_EQUAL_STRING("The roof refused to close; it tries again in 1 minute. Refused: x", alertFor(d, "x").message.c_str());
    Decision s;
    s.event = Event::StoppedByPerson;
    TEST_ASSERT_EQUAL_STRING(
        "A person stopped the roof during the automatic close. It stays open while unsafe until someone closes it or the weather "
        "has been safe for 10 minutes.",
        alertFor(s, "").message.c_str());
    s.rearmMs = 3600000;
    TEST_ASSERT_EQUAL_STRING(
        "A person stopped the roof during the automatic close. It stays open while unsafe until someone closes it or the weather "
        "has been safe for 60 minutes.",
        alertFor(s, "").message.c_str());
}

void test_alert_resend_cap_follows_the_reminder(void)
{
    AlertRetry retry;
    retry.setMaxGap(600000);
    retry.sent(0);
    const uint64_t gaps[] = {120000, 240000, 480000, 600000, 600000};
    for (const uint64_t gap : gaps)
    {
        retry.resent(0);
        TEST_ASSERT_EQUAL_UINT64(gap, retry.gapMs());
    }
    AlertRetry plain; // the default cap is spec 005's 30 minutes
    plain.sent(0);
    for (int i = 0; i < 10; ++i)
        plain.resent(0);
    TEST_ASSERT_EQUAL_UINT64(ALERT_RETRY_MAX_MS, plain.gapMs());
    AlertRetry floor; // never below the first gap
    floor.setMaxGap(0);
    floor.sent(0);
    floor.resent(0);
    TEST_ASSERT_EQUAL_UINT64(ALERT_RETRY_MS, floor.gapMs());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_the_constants_of_specs_005_and_013);
    RUN_TEST(test_default_config_gives_the_default_policy);
    RUN_TEST(test_config_without_the_new_settings_loads_the_defaults);
    RUN_TEST(test_settings_round_trip_and_bad_choice_is_refused);
    RUN_TEST(test_clamp_holds_every_value_to_its_range);
    RUN_TEST(test_config_limits_match_the_logic_ranges);
    RUN_TEST(test_park_hold_limits);
    RUN_TEST(test_park_request_spacing_limits);
    RUN_TEST(test_back_off_limits);
    RUN_TEST(test_rearm_spell_limits);
    RUN_TEST(test_close_guard_limits);
    RUN_TEST(test_not_started_halts_by_default);
    RUN_TEST(test_not_started_retry_backs_off);
    RUN_TEST(test_not_started_retry_survives_a_restart);
    RUN_TEST(test_reminder_limits);
    RUN_TEST(test_long_reminder_keeps_the_30_minute_throttle);
    RUN_TEST(test_throttled_stand_down_alerts_within_30_minutes);
    RUN_TEST(test_remote_stop_is_named_in_the_alert);
    RUN_TEST(test_wording_names_the_set_times);
    RUN_TEST(test_alert_resend_cap_follows_the_reminder);
    return UNITY_END();
}
