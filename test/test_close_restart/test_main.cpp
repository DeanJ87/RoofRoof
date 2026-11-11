// The close-on-unsafe sequence across a restart, the resend of an alert no
// channel took, and a roof controller that didn't start (spec 005;
// constitution III: a board that keeps restarting in the rain still reaches
// its time limits and alerts; an alert counts once a channel has taken it).

#include "../close_rig/CloseRig.h"

#include "AlertRetry.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t BASE = 7ull * 24 * 3600 * 1000; // the glue's time base after a restart

    // A fresh sequence (a restart) restored from `saved`, its first step at `nowMs`.
    Decision restart(Sequence &seq, const Sequence::Saved &saved, Inputs &in, uint64_t nowMs)
    {
        seq = Sequence();
        seq.restore(saved);
        in.nowMs = nowMs;
        in.personStops = 0; // the count starts again
        return seq.step(in);
    }

    Sequence::Saved standDown(Phase phase, Event event, Cause cause)
    {
        Sequence::Saved saved;
        saved.phase = phase;
        saved.event = event;
        saved.cause = cause;
        return saved;
    }
} // namespace

void test_a_restored_stand_down_holds_and_reminds_at_once()
{
    Sequence seq;
    Inputs in;
    in.weatherSafe = false; // unknown at boot
    in.roof = State::Open;
    in.parked = true;
    const Decision d = restart(seq, standDown(Phase::HeldByPerson, Event::StoppedByPerson, Cause::None), in, BASE + 3000);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_TRUE(d.reminder);
    for (in.nowMs = BASE + 3100; in.nowMs < BASE + 3600000; in.nowMs += 1000)
        TEST_ASSERT_EQUAL(Action::None, seq.step(in).action);
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, seq.phase());
    TEST_ASSERT_EQUAL(Phase::HeldByPerson, seq.saved().phase);
    TEST_ASSERT_EQUAL(Event::StoppedByPerson, seq.saved().event);
    restart(seq, standDown(Phase::Halted, Event::CloseFailed, Cause::TooLong), in, BASE + 4000);
    TEST_ASSERT_EQUAL(Phase::Halted, seq.phase());
    TEST_ASSERT_EQUAL(Cause::TooLong, seq.saved().cause);
}

void test_restore_ignores_values_that_make_no_sense()
{
    Sequence seq;
    seq.restore(standDown(Phase::HeldByPerson, static_cast<Event>(200), Cause::None));
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(standDown(Phase::HeldByPerson, Event::Started, Cause::None));
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(standDown(Phase::HeldByPerson, Event::None, Cause::None)); // a stand-down needs its event
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(standDown(Phase::Halted, Event::CloseFailed, static_cast<Cause>(77)));
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(standDown(Phase::CannotMove, Event::CannotClose, Cause::FaultLatched));
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    seq.restore(standDown(static_cast<Phase>(99), Event::None, Cause::None));
    TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    Decision restored;
    restored.event = Event::CloseFailed;
    restored.cause = Cause::TooLong;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(restored, "stopped").message.c_str(), "didn't finish in time"));
}

void test_nothing_is_kept_while_idle_and_a_close_is_kept()
{
    Rig r;
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.saved().phase);
    r.startAndClose();
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.phase());
    TEST_ASSERT_EQUAL(Phase::Closing, r.seq.saved().phase);
}

void test_a_restart_during_a_close_is_a_failed_close()
{
    // A brown-out as the motor starts, every time: the close fails and alerts,
    // and isn't tried again for 5 minutes.
    Sequence::Saved closing;
    closing.phase = Phase::Closing;
    Sequence seq;
    Inputs in;
    in.weatherSafe = false;
    in.roof = State::Open; // still at the open magnet
    in.parked = true;
    const Decision d = restart(seq, closing, in, BASE + 3000);
    assertEvent(d, Event::CloseFailed, Cause::Interrupted);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_EQUAL(Phase::RetryWait, seq.phase());
    for (in.nowMs = BASE + 3100; in.nowMs < BASE + 3000 + RETRY_AFTER_MS; in.nowMs += 1000)
        TEST_ASSERT_EQUAL(Action::None, seq.step(in).action);
    in.nowMs = BASE + 3000 + RETRY_AFTER_MS;
    TEST_ASSERT_EQUAL(Action::Close, seq.step(in).action); // the park has held: one more try
}

void test_a_restored_limit_out_of_range_is_ignored()
{
    Sequence::Saved saved;
    saved.phase = Phase::WaitPark;
    saved.limitMs = 40ull * 24 * 3600 * 1000; // garbage that passed the check
    Sequence seq;
    Inputs in;
    in.weatherSafe = false;
    in.roof = State::Open;
    restart(seq, saved, in, BASE + 1000);
    TEST_ASSERT_EQUAL_UINT64(DEFAULT_PARK_TIMEOUT_MS, seq.saved().limitMs);
    Decision park;
    park.event = Event::ParkNotConfirmed;
    park.cause = Cause::ParkTimeout;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(park, "").message.c_str(), "wasn't confirmed in time"));
}

void test_an_attempt_keeps_its_time_across_restarts()
{
    // The board restarts every 100 s in the rain; N.I.N.A. never confirms the
    // park. The park limit (60 s here, unsafe time) still runs out and alerts.
    Sequence seq;
    seq.setParkTimeoutMs(60000);
    Inputs in;
    in.weatherSafe = false;
    in.roof = State::Open;
    in.nowMs = BASE + 1000;
    TEST_ASSERT_EQUAL(Event::Started, seq.step(in).event);
    in.nowMs = BASE + 40000;
    seq.step(in);
    Sequence::Saved saved = seq.saved();
    TEST_ASSERT_EQUAL(Phase::WaitPark, saved.phase);
    TEST_ASSERT_EQUAL_UINT64(39000, saved.unsafeMs);
    TEST_ASSERT_EQUAL_UINT64(60000, saved.limitMs);
    // Restart: the new boot's clock starts again from the base.
    Decision d = restart(seq, saved, in, BASE + 3000);
    TEST_ASSERT_EQUAL(Phase::WaitPark, seq.phase());
    TEST_ASSERT_EQUAL(Event::None, d.event);
    Decision last;
    // 39 s kept + 10 s for the restart itself: 11 s left of the 60 s limit.
    for (in.nowMs = BASE + 3100; in.nowMs < BASE + 3000 + 11000 - 100; in.nowMs += 100)
        TEST_ASSERT_EQUAL(Event::None, seq.step(in).event);
    in.nowMs = BASE + 3000 + 11000;
    last = seq.step(in);
    assertEvent(last, Event::ParkNotConfirmed, Cause::ParkTimeout);
    TEST_ASSERT_TRUE(last.alert);
    // The back-off is kept too.
    in.nowMs += 100000;
    seq.step(in);
    saved = seq.saved();
    TEST_ASSERT_EQUAL(Phase::RetryWait, saved.phase);
    TEST_ASSERT_EQUAL(Event::ParkNotConfirmed, saved.event);
    d = restart(seq, saved, in, BASE + 2000);
    TEST_ASSERT_EQUAL(Phase::RetryWait, seq.phase());
    TEST_ASSERT_EQUAL(Action::None, d.action);
    for (in.nowMs = BASE + 2100; in.nowMs < BASE + 2000 + RETRY_AFTER_MS - 100000 - RESTART_ALLOWANCE_MS - 1000; in.nowMs += 1000)
        TEST_ASSERT_EQUAL(Action::None, seq.step(in).action);
    in.nowMs = BASE + 2000 + RETRY_AFTER_MS - 100000 - RESTART_ALLOWANCE_MS;
    TEST_ASSERT_EQUAL(Action::RequestPark, seq.step(in).action);
}

void test_a_restart_loop_still_raises_mount_not_parked()
{
    Sequence seq;
    Inputs in;
    in.weatherSafe = false;
    in.roof = State::Open;
    bool alerted = false;
    uint64_t total = 0;
    for (int boot = 0; boot < 20 && !alerted; ++boot)
    {
        if (boot > 0)
            restart(seq, seq.saved(), in, BASE + 3000);
        for (in.nowMs = BASE + 3000; in.nowMs < BASE + 3000 + 120000; in.nowMs += 500, total += 500) // 2 min per boot
            alerted |= seq.step(in).event == Event::ParkNotConfirmed;
    }
    TEST_ASSERT_TRUE(alerted);
    TEST_ASSERT_TRUE(total <= DEFAULT_PARK_TIMEOUT_MS + 120000);
}

void test_a_restored_time_too_long_for_the_new_time_base_is_clamped()
{
    Sequence seq;
    Sequence::Saved saved;
    saved.phase = Phase::WaitPark;
    saved.unsafeMs = 50000;
    saved.wallMs = 50000;
    saved.limitMs = 60000;
    Inputs in;
    in.weatherSafe = false;
    in.roof = State::Open;
    restart(seq, saved, in, 2000); // a base younger than the time kept
    TEST_ASSERT_EQUAL(Phase::WaitPark, seq.phase());
    TEST_ASSERT_EQUAL_UINT64(2000, seq.saved().unsafeMs);
    // A crash just after a start, again and again, still makes progress: each restart counts.
    Sequence loop;
    loop.setParkTimeoutMs(60000);
    Sequence::Saved kept;
    kept.phase = Phase::WaitPark;
    kept.limitMs = 60000;
    int restarts = 0;
    while (restarts < 10)
    {
        restart(loop, kept, in, BASE + 1000);
        ++restarts;
        if (loop.phase() == Phase::RetryWait)
            break;
        kept = loop.saved();
    }
    TEST_ASSERT_TRUE(restarts <= 7); // 60 s / 10 s per restart
}

void test_a_roof_that_didnt_start_names_that_cause()
{
    Rig r;
    r.in.roof = State::Unknown;
    r.in.roofStarted = false;
    r.at(T0 - 50);
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 - 50 + UNKNOWN_CONFIRM_MS);
    assertEvent(d, Event::CannotClose, Cause::RoofNotStarted);
    TEST_ASSERT_NOT_NULL(strstr(alertFor(d, "").message.c_str(), "didn't start"));
    TEST_ASSERT_EQUAL_STRING("roofNotStarted", causeName(Cause::RoofNotStarted));
}

void test_an_alert_no_channel_took_is_sent_again_every_minute()
{
    AlertRetry retry;
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Failed, 1000)); // nothing sent yet
    retry.sent(1000);
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Pending, 1000 + ALERT_RETRY_MS));
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Failed, 1000 + ALERT_RETRY_MS - 1));
    TEST_ASSERT_TRUE(retry.resendDue(Delivery::Failed, 1000 + ALERT_RETRY_MS));
    retry.sent(1000 + ALERT_RETRY_MS);
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Failed, 1000 + ALERT_RETRY_MS + 1));
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Delivered, 1000 + 3 * ALERT_RETRY_MS)); // a channel took it
    TEST_ASSERT_FALSE(retry.active());
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Failed, 1000 + 9 * ALERT_RETRY_MS));
}

void test_no_resend_without_a_channel_or_once_idle()
{
    AlertRetry retry;
    retry.sent(0);
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::NoChannel, ALERT_RETRY_MS)); // nothing could take it
    TEST_ASSERT_FALSE(retry.active());
    retry.sent(0);
    retry.clear(); // the sequence is idle
    TEST_ASSERT_FALSE(retry.resendDue(Delivery::Failed, 10 * ALERT_RETRY_MS));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_restored_stand_down_holds_and_reminds_at_once);
    RUN_TEST(test_restore_ignores_values_that_make_no_sense);
    RUN_TEST(test_nothing_is_kept_while_idle_and_a_close_is_kept);
    RUN_TEST(test_a_restart_during_a_close_is_a_failed_close);
    RUN_TEST(test_a_restored_limit_out_of_range_is_ignored);
    RUN_TEST(test_an_attempt_keeps_its_time_across_restarts);
    RUN_TEST(test_a_restart_loop_still_raises_mount_not_parked);
    RUN_TEST(test_a_restored_time_too_long_for_the_new_time_base_is_clamped);
    RUN_TEST(test_a_roof_that_didnt_start_names_that_cause);
    RUN_TEST(test_an_alert_no_channel_took_is_sent_again_every_minute);
    RUN_TEST(test_no_resend_without_a_channel_or_once_idle);
    return UNITY_END();
}
