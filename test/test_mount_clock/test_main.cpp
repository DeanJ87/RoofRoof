// Spec 051 (finding F60): N.I.N.A. answering with frozen mount values. When the
// mount's serial link dies the ASCOM driver can stay connected on a dead port,
// and N.I.N.A.'s mount/info goes on saying Connected, AtPark, Slewing and
// SiderealTime as they last were. A reading whose SiderealTime hasn't moved for
// MOUNT_CLOCK_STALE_MS is unreadable (frozen): the park is unknown, no park is
// confirmed from it, the park request treats it as unreadable (spec 050), and a
// move under way follows spec 047 (it goes on only on N.I.N.A.'s last answer).

#include <cstdint>
#include <unity.h>

#include "MountClock.h"
#include "NinaLost.h"
#include "ParkRule.h"
#include "ParkSend.h"

using namespace SQM;
using Nina::ClockChange;
using Nina::MountClockWatch;
using Nina::MountReading;
using Nina::ReadingStatus;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr double HOUR_MS = 3600000.0;
    constexpr double SIDEREAL = 1.00273790935;

    // A good reading of a connected mount at `atMs` with the clock at `hours`.
    MountReading reading(uint64_t atMs, double hours, bool atPark = true, bool slewing = false)
    {
        MountReading r;
        r.status = ReadingStatus::Good;
        r.connected = true;
        r.atPark = atPark;
        r.slewing = slewing;
        r.takenAtMs = atMs;
        r.hasSiderealTime = true;
        r.siderealHours = hours;
        return r;
    }

    // The clock as a live mount reads it `atMs` after it read `startHours`.
    double liveHours(double startHours, uint64_t atMs)
    {
        double h = startHours + static_cast<double>(atMs) * SIDEREAL / HOUR_MS;
        while (h >= 24.0)
            h -= 24.0;
        return h;
    }

    // Feeds the watch one reading every `stepMs` from `fromMs` to `toMs` with a
    // frozen clock; returns the last reading as the watch left it.
    MountReading feedFrozen(MountClockWatch &watch, uint64_t fromMs, uint64_t toMs, double hours, uint64_t stepMs = 2000)
    {
        MountReading last;
        for (uint64_t t = fromMs; t <= toMs; t += stepMs)
        {
            last = reading(t, hours);
            watch.apply(last);
        }
        return last;
    }

    Safety::ParkResult parkOf(const MountReading &r, uint64_t nowMs)
    {
        Safety::ParkInputs in;
        in.nina = r;
        in.nowMs = nowMs;
        return Safety::evaluatePark(in);
    }
} // namespace

void test_advancing_clock_is_live(void)
{
    MountClockWatch watch;
    for (uint64_t t = 0; t <= 120000; t += 2000)
    {
        MountReading r = reading(t, liveHours(5.2, t));
        TEST_ASSERT_TRUE(watch.apply(r) == ClockChange::None);
        TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
        TEST_ASSERT_TRUE(r.atPark);
        TEST_ASSERT_FALSE(r.frozen);
    }
    TEST_ASSERT_FALSE(watch.frozen());
}

// N.I.N.A. refreshes its values every few seconds: a clock that steps every
// 5 s (the same value read twice in a row) is live.
void test_clock_stepping_every_few_seconds_is_live(void)
{
    MountClockWatch watch;
    for (uint64_t t = 0; t <= 120000; t += 2000)
    {
        const uint64_t refreshed = (t / 5000) * 5000;
        MountReading r = reading(t, liveHours(5.2, refreshed));
        watch.apply(r);
        TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
    }
}

void test_frozen_for_20_s_is_stale_and_park_unknown(void)
{
    MountClockWatch watch;
    MountReading r = feedFrozen(watch, 0, 18000, 5.2);
    TEST_ASSERT_TRUE(r.status == ReadingStatus::Good); // 18 s: not yet
    TEST_ASSERT_TRUE(parkOf(r, 18000).parked);
    r = reading(20000, 5.2);
    TEST_ASSERT_TRUE(watch.apply(r) == ClockChange::Frozen);
    TEST_ASSERT_TRUE(watch.frozen());
    TEST_ASSERT_TRUE(r.status == ReadingStatus::Unreadable);
    TEST_ASSERT_TRUE(r.frozen);
    TEST_ASSERT_FALSE(r.atPark); // AtPark=true from N.I.N.A.'s cache: not carried
    TEST_ASSERT_EQUAL_STRING(Nina::MOUNT_FROZEN_TEXT, r.detail.c_str());
    TEST_ASSERT_EQUAL_UINT64(20000, r.takenAtMs);
    // Logged once: further frozen readings are frozen without a new change.
    MountReading again = reading(22000, 5.2);
    TEST_ASSERT_TRUE(watch.apply(again) == ClockChange::None);
    TEST_ASSERT_TRUE(again.frozen);
}

void test_frozen_reading_never_confirms_the_park(void)
{
    MountClockWatch watch;
    const MountReading r = feedFrozen(watch, 0, 26000, 5.2);
    const Safety::ParkResult park = parkOf(r, 26000);
    TEST_ASSERT_FALSE(park.parked);
    TEST_ASSERT_TRUE((park.reasons & Safety::ParkNinaUnreadable) != 0);
    TEST_ASSERT_TRUE(park.ninaFrozen);
    TEST_ASSERT_EQUAL_STRING(
        "not parked: N.I.N.A.'s mount values have stopped updating (mount connection stuck?), park unknown",
        Safety::describe(park).c_str());
    // N.I.N.A. counts as lost (spec 030's close, spec 027's alerts) and with
    // switches it stays unknown even with every switch closed.
    TEST_ASSERT_TRUE(CloseSeq::ninaLost(park.reasons));
    Safety::ParkInputs both;
    both.nina = r;
    both.nowMs = 26000;
    both.fittedSwitches = 2;
    both.switch1 = Safety::SwitchReading::Closed;
    both.switch2 = Safety::SwitchReading::Closed;
    TEST_ASSERT_FALSE(Safety::evaluatePark(both).parked);
}

// Spec 047: a move under way treats it as N.I.N.A. not answering: it goes on
// only on the last answer having said parked; nothing starts on it.
void test_move_under_way_follows_spec_047(void)
{
    MountClockWatch watch;
    Safety::NinaMemory memory;
    MountReading r;
    for (uint64_t t = 0; t <= 40000; t += 2000)
    {
        r = reading(t, 5.2);
        watch.apply(r);
        Safety::rememberNina(memory, r, 1);
    }
    TEST_ASSERT_TRUE(r.frozen);
    Safety::ParkInputs in;
    in.nina = r;
    in.nowMs = 40000;
    const Safety::ParkResult park = Safety::evaluatePark(in);
    const Safety::CloseHold hold = Safety::closeHold(in, park, memory);
    TEST_ASSERT_FALSE(hold.parked); // never starts a move
    TEST_ASSERT_TRUE(hold.unanswered);
    // A frozen "not at park" never lets a move go on.
    MountClockWatch watch2;
    Safety::NinaMemory memory2;
    for (uint64_t t = 0; t <= 40000; t += 2000)
    {
        r = reading(t, 5.2, false);
        watch2.apply(r);
        Safety::rememberNina(memory2, r, 1);
    }
    in.nina = r;
    TEST_ASSERT_FALSE(Safety::closeHold(in, Safety::evaluatePark(in), memory2).unanswered);
}

void test_recovers_when_the_clock_moves_again(void)
{
    MountClockWatch watch;
    feedFrozen(watch, 0, 30000, 5.2);
    TEST_ASSERT_TRUE(watch.frozen());
    MountReading r = reading(32000, liveHours(5.2, 32000));
    TEST_ASSERT_TRUE(watch.apply(r) == ClockChange::Live);
    TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
    TEST_ASSERT_TRUE(parkOf(r, 32000).parked);
    TEST_ASSERT_FALSE(watch.frozen());
    // And freezes again 20 s after its last move.
    MountReading later = feedFrozen(watch, 34000, 50000, r.siderealHours);
    TEST_ASSERT_TRUE(later.status == ReadingStatus::Good);
    later = reading(52000, r.siderealHours);
    TEST_ASSERT_TRUE(watch.apply(later) == ClockChange::Frozen);
}

void test_24_h_wrap(void)
{
    MountClockWatch watch;
    // 23:59:50 sidereal, live across midnight.
    const double start = 24.0 - 10.0 / 3600.0;
    for (uint64_t t = 0; t <= 60000; t += 2000)
    {
        MountReading r = reading(t, liveHours(start, t));
        watch.apply(r);
        TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
    }
    TEST_ASSERT_FALSE(watch.frozen());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2000.0f, static_cast<float>(Nina::siderealStepMs(start, liveHours(start, 2000))));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 20000.0f, static_cast<float>(Nina::siderealStepMs(23.999, liveHours(23.999, 20000))));
    // Frozen just after midnight is still frozen.
    MountClockWatch frozen;
    TEST_ASSERT_TRUE(feedFrozen(frozen, 0, 20000, 0.0001).frozen);
}

// N.I.N.A. restarted, or away for a while: the check starts again (not stale).
void test_gap_or_restart_resets_the_baseline(void)
{
    MountClockWatch watch;
    feedFrozen(watch, 0, 14000, 5.2);
    // 30 s without a good reading, then the same value: a new baseline.
    MountReading r = reading(44000, 5.2);
    watch.apply(r);
    TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
    // Frozen only 20 s after the new baseline.
    TEST_ASSERT_TRUE(feedFrozen(watch, 46000, 62000, 5.2).status == ReadingStatus::Good);
    TEST_ASSERT_TRUE(feedFrozen(watch, 64000, 64000, 5.2).frozen);

    // A restarted N.I.N.A. on another clock: a step back, or far ahead, is live.
    MountClockWatch jump;
    feedFrozen(jump, 0, 10000, 5.2);
    MountReading back = reading(12000, 4.0);
    jump.apply(back);
    TEST_ASSERT_TRUE(back.status == ReadingStatus::Good);
    MountReading ahead = reading(14000, 9.0);
    jump.apply(ahead);
    TEST_ASSERT_TRUE(ahead.status == ReadingStatus::Good);
    // ...and judged from the new clock on.
    TEST_ASSERT_TRUE(feedFrozen(jump, 16000, 32000, 9.0).status == ReadingStatus::Good);
    TEST_ASSERT_TRUE(feedFrozen(jump, 34000, 34000, 9.0).frozen);

    // Failed reads in between (shorter than the gap) don't hide a frozen clock.
    MountClockWatch flaky;
    MountReading first = reading(0, 5.2);
    flaky.apply(first);
    for (uint64_t t = 2000; t < 20000; t += 2000)
    {
        MountReading lost = Nina::failedReading(ReadingStatus::Unreachable, "no reply", t);
        TEST_ASSERT_TRUE(flaky.apply(lost) == ClockChange::None);
        TEST_ASSERT_TRUE(lost.status == ReadingStatus::Unreachable);
    }
    MountReading back20 = reading(20000, 5.2);
    flaky.apply(back20);
    TEST_ASSERT_TRUE(back20.frozen);
}

void test_new_settings_reset(void)
{
    MountClockWatch watch;
    feedFrozen(watch, 0, 24000, 5.2);
    TEST_ASSERT_TRUE(watch.reset() == ClockChange::Unjudged);
    TEST_ASSERT_TRUE(watch.reset() == ClockChange::None);
    MountReading r = reading(26000, 5.2);
    watch.apply(r);
    TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
}

// No SiderealTime (an older plugin, a driver without it), or no mount: as before.
void test_missing_clock_keeps_the_old_behaviour(void)
{
    MountClockWatch watch;
    for (uint64_t t = 0; t <= 120000; t += 2000)
    {
        MountReading r = reading(t, 5.2);
        r.hasSiderealTime = false;
        watch.apply(r);
        TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
        TEST_ASSERT_TRUE(parkOf(r, t).parked);
    }
    // A frozen clock that then disappears: no longer judged, the reading as it is.
    MountClockWatch was;
    feedFrozen(was, 0, 24000, 5.2);
    MountReading none = reading(26000, 5.2);
    none.hasSiderealTime = false;
    TEST_ASSERT_TRUE(was.apply(none) == ClockChange::Unjudged);
    TEST_ASSERT_TRUE(none.status == ReadingStatus::Good);
    // Not connected: nothing to judge.
    MountClockWatch gone;
    for (uint64_t t = 0; t <= 60000; t += 2000)
    {
        MountReading r = reading(t, 5.2);
        r.connected = false;
        gone.apply(r);
        TEST_ASSERT_TRUE(r.status == ReadingStatus::Good);
        TEST_ASSERT_FALSE(r.frozen);
    }
}

// Spec 050: a frozen reading is neither parked (no AlreadyParked) nor still:
// the park request treats it as unreadable.
void test_park_sender_with_a_frozen_reading(void)
{
    MountClockWatch watch;
    MountReading r = feedFrozen(watch, 0, 24000, 5.2); // cached AtPark true
    TEST_ASSERT_TRUE(r.frozen);
    Safety::ParkSender sender;
    TEST_ASSERT_TRUE(sender.request(25000));
    Safety::ParkSendInputs in;
    in.nowMs = 26000;
    in.reading = reading(26000, 5.2);
    watch.apply(in.reading);
    in.roofAtEnd = true;
    TEST_ASSERT_TRUE(in.reading.frozen);
    TEST_ASSERT_TRUE(sender.step(in) == Safety::ParkSendEvent::Send); // as unreadable: sent, not "already parked"

    // Frozen while slewing: waits while the last good reading is fresh, then
    // sends as for any unreadable reading.
    MountClockWatch slewWatch;
    Safety::ParkSender slewSender;
    for (uint64_t t = 0; t <= 18000; t += 2000)
    {
        Safety::ParkSendInputs s;
        s.nowMs = t;
        s.reading = reading(t, 5.2, false, true);
        slewWatch.apply(s.reading);
        s.roofAtEnd = true;
        slewSender.step(s);
    }
    TEST_ASSERT_TRUE(slewSender.request(19000));
    Safety::ParkSendInputs s;
    s.roofAtEnd = true;
    s.nowMs = 20000;
    s.reading = reading(20000, 5.2, false, true);
    slewWatch.apply(s.reading);
    TEST_ASSERT_TRUE(s.reading.frozen);
    TEST_ASSERT_TRUE(slewSender.step(s) == Safety::ParkSendEvent::Waiting); // last good (18 s) fresh and slewing
    s.nowMs = 30000;
    s.reading = reading(30000, 5.2, false, true);
    slewWatch.apply(s.reading);
    TEST_ASSERT_TRUE(slewSender.step(s) == Safety::ParkSendEvent::Send); // past its freshness: sent
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_advancing_clock_is_live);
    RUN_TEST(test_clock_stepping_every_few_seconds_is_live);
    RUN_TEST(test_frozen_for_20_s_is_stale_and_park_unknown);
    RUN_TEST(test_frozen_reading_never_confirms_the_park);
    RUN_TEST(test_move_under_way_follows_spec_047);
    RUN_TEST(test_recovers_when_the_clock_moves_again);
    RUN_TEST(test_24_h_wrap);
    RUN_TEST(test_gap_or_restart_resets_the_baseline);
    RUN_TEST(test_new_settings_reset);
    RUN_TEST(test_missing_clock_keeps_the_old_behaviour);
    RUN_TEST(test_park_sender_with_a_frozen_reading);
    return UNITY_END();
}
