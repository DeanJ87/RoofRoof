// The roof controller's clocks and limits (spec 002 FR-002, FR-008, FR-025,
// FR-026; added after the safety review): the dead time timed by control steps
// only and applied after start-up, the drive budget across restarts, abort at
// rest, and readings too implausible to believe.

#include <cmath>
#include <cstdio>
#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "MotionProfile.h"
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // Opens a closed bench roof part-way: `ms` of driving.
    void openFor(Bench &b, uint32_t ms)
    {
        TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
        for (uint32_t t = 0; t < ms; t += b.stepMs)
            b.step();
    }
} // namespace

namespace
{
    // Readings for a controller stepped by hand with chosen step times: at the
    // closed magnet (700 counts) unless `hallCounts` says otherwise.
    Readings closedReadings(uint32_t nowMs, uint16_t hallCounts = 700)
    {
        Readings r;
        r.nowMs = nowMs;
        r.hallCounts = hallCounts;
        r.supplyVolts = 12.6f;
        r.motorAmps = 3.0f;
        return r;
    }

    // Steps `roof` every 20 ms from `nowMs` (updated) until it drives; returns the
    // time of the first driven step, or 0 if it never drove within `maxMs`.
    uint32_t firstDrivenStep(Controller &roof, uint32_t &nowMs, uint32_t maxMs, uint16_t hallCounts = 700)
    {
        for (uint32_t waited = 0; waited <= maxMs; waited += 20)
        {
            nowMs += 20;
            if (roof.tick(closedReadings(nowMs, hallCounts)).command != Driver::Command::Off)
                return nowMs;
        }
        return 0;
    }
} // namespace

// constitution I, review finding: the dead time can't be skipped by a command
// whose clock is ahead of, or behind, the control loop's. Commands carry no time
// at all, so the only clock is the steps': whatever happens between two steps,
// the driver is off for DEAD_TIME_MS of step time after a stop.
void test_dead_time_is_timed_by_steps_only(void)
{
    // Between the magnets (2000 counts), so a person's moves run either way.
    constexpr uint16_t BETWEEN = 2000;
    Controller roof;
    uint32_t nowMs = 1000;
    for (int i = 0; i < 30; ++i)
        roof.tick(closedReadings(nowMs += 20, BETWEEN));
    TEST_ASSERT_TRUE(roof.command(Command::Open, Origin::Person).accepted);
    TEST_ASSERT_TRUE(roof.tick(closedReadings(nowMs += 20, BETWEEN)).command != Driver::Command::Off);
    roof.tick(closedReadings(nowMs += 20, BETWEEN));
    // Reverse between two steps, then the next step comes only 2 ms later (a
    // command handled late in the loop, or a step clock behind the caller's).
    TEST_ASSERT_TRUE(roof.command(Command::Close, Origin::Person).accepted);
    TEST_ASSERT_TRUE(roof.output().command == Driver::Command::Off);
    nowMs += 2;
    const uint32_t firstStepMs = nowMs;
    TEST_ASSERT_TRUE(roof.tick(closedReadings(nowMs, BETWEEN)).command == Driver::Command::Off);
    const uint32_t drivenAt = firstDrivenStep(roof, nowMs, 2000, BETWEEN);
    TEST_ASSERT_TRUE(drivenAt != 0);
    TEST_ASSERT_TRUE(drivenAt - firstStepMs >= DEAD_TIME_MS);
    TEST_ASSERT_TRUE(roof.output().command == Driver::Command::Reverse);
}

// The first drive after start-up also waits the dead time: construction counts as
// a stop, timed from the first step.
void test_the_first_drive_after_start_up_waits_the_dead_time(void)
{
    Controller roof;
    uint32_t nowMs = 5000;
    const uint32_t firstStepMs = nowMs + 20;
    for (int i = 0; i < 3; ++i) // the Hall settles at the closed magnet
        roof.tick(closedReadings(nowMs += 20));
    TEST_ASSERT_TRUE(roof.command(Command::Open, Origin::Person).accepted);
    const uint32_t drivenAt = firstDrivenStep(roof, nowMs, 2000);
    TEST_ASSERT_EQUAL_UINT32(firstStepMs + DEAD_TIME_MS, drivenAt);
}

// Review finding: Stop and Open again, over and over, can't give a jammed roof a
// fresh timeout each time. The total driving since a magnet is capped.
void test_repeated_restarts_hit_the_drive_budget(void)
{
    Bench b;
    openFor(b, 5000);
    b.sim.jammed = true; // stuck part-way; current sense off
    b.command(Command::Stop);
    uint32_t restarts = 0;
    while (b.roof.status().fault.kind == FaultKind::None && restarts < 1000)
    {
        b.command(Command::Open, Origin::Automatic);
        b.steps(10000 / 20); // 10 s of each restart, far below its own timeout
        b.command(Command::Stop, Origin::Automatic);
        b.steps(30);
        ++restarts;
    }
    const Fault fault = b.roof.status().fault;
    TEST_ASSERT_TRUE(fault.kind == FaultKind::DriveBudget);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(driveBudgetMs(Settings())), fault.limit);
    TEST_ASSERT_TRUE(fault.measured >= fault.limit && fault.measured < fault.limit + 20.0f);
    TEST_ASSERT_TRUE(restarts > 5);
    // A person clearing it gives a fresh budget.
    TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    b.steps(100);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

namespace
{
    // Opens a closed roof for `openMs` (stopping short of the open magnet), then
    // reverses to close; true if it ends closed with no fault.
    bool reversalCompletes(const Settings &s, uint32_t openMs)
    {
        Bench b(s);
        b.command(Command::Open);
        for (uint32_t t = 0; t < openMs && b.state() == State::Opening; t += b.stepMs)
            b.step();
        if (b.state() != State::Opening)
            return true; // reached the end first: not a reversal part-way
        if (!b.command(Command::Close).accepted)
            return false;
        b.runToRest(2000000);
        return b.state() == State::Closed && b.roof.status().fault.kind == FaultKind::None;
    }
} // namespace

// Review finding: one legitimate reversal part-way can need nearly two travels of
// driving between magnets. The budget is per direction, so it never trips on it,
// whatever the duties (the cases the review reproduced, and the extremes).
void test_a_reversal_near_the_end_never_trips_the_drive_budget(void)
{
    struct Case
    {
        uint8_t top;
        uint8_t slow;
        uint32_t openMs;
    };
    const Case cases[] = {
        {60, 40, 75000},
        {50, 40, 90000},
        {100, 100, 45000},
        {100, 80, 45000},
        {20, 20, 240000},
        {20, 10, 240000},
        {100, 10, 48000},
    };
    for (const Case &c : cases)
    {
        Settings s;
        s.topDutyPercent = c.top;
        s.slowDutyPercent = c.slow;
        s.timeoutMarginMs = 3000; // the tightest margin
        char message[64];
        snprintf(message, sizeof(message), "top %u slow %u open %u ms", c.top, c.slow, static_cast<unsigned>(c.openMs));
        TEST_ASSERT_TRUE_MESSAGE(reversalCompletes(s, c.openMs), message);
    }
}

// Cycling one way still trips: the per-direction budget keeps the jam protection.
void test_the_budget_still_trips_restarts_in_one_direction(void)
{
    Bench b;
    openFor(b, 2000);
    b.sim.jammed = true;
    b.command(Command::Stop);
    for (int i = 0; i < 100 && b.roof.status().fault.kind == FaultKind::None; ++i)
    {
        b.command(Command::Open, Origin::Automatic);
        b.steps(5000 / 20);
        b.command(Command::Stop, Origin::Automatic);
        b.steps(30);
    }
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::DriveBudget);
}

// Hardening: a step clock that goes backwards (an integration bug) counts as still
// inside the dead time, never as past it.
void test_a_step_clock_going_backwards_keeps_the_driver_off(void)
{
    Bench b;
    openFor(b, 3000);
    b.command(Command::Close);
    b.step();                                    // the dead time starts here
    Readings r = b.sim.readings(b.nowMs - 1000); // 1 s back
    for (int i = 0; i < 10; ++i)
    {
        r.nowMs += 20;
        TEST_ASSERT_TRUE(b.roof.tick(r).command == Driver::Command::Off);
    }
}

namespace
{
    // A roof stopped part-way after a drive, then left idle for `idleMs` (ticked
    // every `idleStepMs`), then asked to close: the time from the command to the
    // first driven step, or 0 if it never drove within 2 s.
    uint32_t driveDelayAfterIdle(uint64_t idleMs, uint32_t idleStepMs)
    {
        Bench b;
        TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
        b.steps(5000 / 20);
        b.command(Command::Stop);
        Readings r = b.sim.readings(b.nowMs);
        for (uint64_t idle = 0; idle < idleMs; idle += idleStepMs)
        {
            const uint32_t step = static_cast<uint32_t>(idleMs - idle < idleStepMs ? idleMs - idle : idleStepMs);
            r.nowMs += step;
            TEST_ASSERT_TRUE(b.roof.tick(r).command == Driver::Command::Off);
        }
        b.nowMs = r.nowMs;
        if (!b.command(Command::Close).accepted)
            return 0;
        const uint32_t commandedAt = b.nowMs;
        for (int i = 0; i < 100; ++i)
            if (b.step().command != Driver::Command::Off)
                return b.nowMs - commandedAt;
        return 0;
    }
} // namespace

// Final review: the dead time must not come back after a long idle. The stamp of
// the last stop is never refreshed at rest, so a signed difference recomputed from
// it turns negative after 2^31 ms (24.9 days) and the roof would accept a move and
// never drive. The dead time is latched as done once it has elapsed.
void test_a_move_after_25_days_idle_drives(void)
{
    const uint32_t delay = driveDelayAfterIdle(25ull * 86400000ull, 1000);
    TEST_ASSERT_TRUE(delay > 0 && delay <= 520);
}

void test_a_move_after_48_days_idle_drives(void)
{
    const uint32_t delay = driveDelayAfterIdle(48ull * 86400000ull, 1000);
    TEST_ASSERT_TRUE(delay > 0 && delay <= 520);
}

// Idle gaps around 2^31 and 2^32 ms (the signed and unsigned wraps), reached in
// hour-long idle steps.
void test_idle_gaps_around_the_clock_wraps_never_block_a_move(void)
{
    const uint64_t centres[] = {1ull << 31, 1ull << 32};
    const int64_t offsets[] = {-3600000, -1000, -501, -1, 0, 1, 499, 500, 1000, 3600000};
    for (uint64_t centre : centres)
        for (int64_t offset : offsets)
        {
            const uint64_t idle = static_cast<uint64_t>(static_cast<int64_t>(centre) + offset);
            const uint32_t delay = driveDelayAfterIdle(idle, 3600000);
            char message[64];
            snprintf(
                message, sizeof(message), "idle %llu ms: delay %u", static_cast<unsigned long long>(idle), static_cast<unsigned>(delay));
            TEST_ASSERT_TRUE_MESSAGE(delay > 0 && delay <= 520, message);
        }
}

namespace
{
    constexpr uint64_t THIRTY_DAYS_MS = 30ull * 86400000ull;

    // Ticks a resting bench roof for `idleMs` in one-second steps.
    void idleFor(Bench &b, uint64_t idleMs)
    {
        Readings r = b.sim.readings(b.nowMs);
        for (uint64_t idle = 0; idle < idleMs; idle += 1000)
        {
            r.nowMs += 1000;
            b.roof.tick(r);
        }
        b.nowMs = r.nowMs;
    }
} // namespace

// Audit of every stored time (final review): each is taken when a drive starts or
// a condition first appears, and compared only while driving, so a long idle
// before a move changes nothing. These check each one after 30 days idle.

// The timeout's start (move.driveStartMs): a missing magnet still times out at the limit.
void test_after_a_long_idle_the_timeout_counts_from_this_drive(void)
{
    Bench b;
    b.sim.openMagnetMissing = true;
    idleFor(b, THIRTY_DAYS_MS);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return c.status().fault.kind != FaultKind::None; }, 200000));
    const Fault fault = b.roof.status().fault;
    TEST_ASSERT_TRUE(fault.kind == FaultKind::Timeout);
    TEST_ASSERT_TRUE(fault.measured >= fault.limit && fault.measured < fault.limit + 20.0f);
}

// The leave-magnet check uses the same start.
void test_after_a_long_idle_the_leave_time_counts_from_this_drive(void)
{
    Bench b;
    b.sim.jammed = true;
    idleFor(b, THIRTY_DAYS_MS);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return c.status().fault.kind != FaultKind::None; }, 20000));
    const Fault fault = b.roof.status().fault;
    TEST_ASSERT_TRUE(fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_TRUE(fault.measured >= 10000.0f && fault.measured < 10020.0f);
}

// The confirm windows (Pending::sinceMs): a dip seen in an earlier move doesn't
// carry its start time into a move a month later; the new dip still needs 250 ms.
void test_after_a_long_idle_a_confirm_window_starts_afresh(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(100);
    b.sim.supplyVolts = 10.0f; // a dip starts...
    b.steps(5);
    b.command(Command::Stop); // ...and the move is stopped inside it
    b.sim.supplyVolts = 12.6f;
    idleFor(b, THIRTY_DAYS_MS);
    b.step();
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return c.status().driving; }, 2000));
    b.sim.supplyVolts = 10.0f;
    b.steps(12); // 240 ms of the new dip: not yet
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    b.steps(2);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::SupplyLow);
}

// The same for a condition that never lapses between moves: a current reading
// stuck high at rest (no check runs while the motor is off). The window of the
// earlier move must not carry over and fault the new one on its first step.
void test_after_a_long_idle_a_stuck_condition_still_gets_its_confirm_time(void)
{
    Settings s;
    s.currentSenseEnabled = true;
    Bench b(s);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(100);
    Readings r = b.sim.readings(b.nowMs);
    r.motorAmps = 25.0f;
    for (int i = 0; i < 5; ++i) // over the limit for 100 ms, then stopped
    {
        r.nowMs += 20;
        b.roof.tick(r);
    }
    b.roof.command(Command::Stop, Origin::Person);
    for (uint64_t idle = 0; idle < THIRTY_DAYS_MS; idle += 1000)
    {
        r.nowMs += 1000;
        b.roof.tick(r);
    }
    TEST_ASSERT_TRUE(b.roof.command(Command::Open, Origin::Person).accepted);
    int driven = 0;
    for (int i = 0; i < 40 && b.roof.status().fault.kind == FaultKind::None; ++i)
    {
        r.nowMs += 20;
        b.roof.tick(r);
        if (b.roof.status().driving)
            ++driven;
    }
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::OverCurrent);
    TEST_ASSERT_TRUE(driven >= 12); // 250 ms of driving at 20 ms steps before the fault
}

// The late-step check (lastStepMs) is refreshed every step: a long idle with
// regular steps is no late step, and a late step after it is still caught.
void test_after_a_long_idle_a_late_step_is_still_caught(void)
{
    Bench b;
    idleFor(b, THIRTY_DAYS_MS);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return c.status().driving; }, 2000));
    b.steps(10);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    b.stepMs = MAX_STEP_GAP_MS + 1;
    b.step();
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::LateStep);
}

// Reaching a magnet resets the budget, so a working roof can open and close forever.
void test_reaching_a_magnet_resets_the_drive_budget(void)
{
    Bench b;
    for (int i = 0; i < 6; ++i)
    {
        TEST_ASSERT_TRUE(b.command(i % 2 == 0 ? Command::Open : Command::Close, Origin::Automatic).accepted);
        TEST_ASSERT_TRUE(b.runToRest());
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    }
}

// Abort at rest changes nothing: nothing moved, so a known position stays known
// (Alpaca AbortSlew on a stopped roof).
void test_abort_at_rest_keeps_a_known_position(void)
{
    Bench b;
    openFor(b, 8000);
    b.command(Command::Stop);
    const float estimate = b.roof.status().positionMm;
    TEST_ASSERT_TRUE(b.command(Command::Abort, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_EQUAL_FLOAT(estimate, b.roof.status().positionMm);
}

// An implausible supply reading counts as unreadable, like NaN.
void test_implausible_supply_readings_are_unreadable(void)
{
    for (float volts : {16.01f, 30.0f, INFINITY, -INFINITY})
    {
        Bench b;
        b.sim.supplyVolts = volts;
        b.step();
        const CommandResult r = b.command(Command::Open);
        TEST_ASSERT_TRUE(r.reason == Refusal::SupplyUnreadable);
    }
    Bench ok;
    ok.sim.supplyVolts = 16.0f; // the highest believable reading
    ok.step();
    TEST_ASSERT_TRUE(ok.command(Command::Open).accepted);
}

// With current sense on, a negative current beyond the ADC's offset noise is broken.
void test_negative_current_is_unreadable_with_current_sense_on(void)
{
    Settings s;
    s.currentSenseEnabled = true;
    Bench b(s);
    b.sim.runningAmps = -0.6f;
    Readings r = b.sim.readings(b.nowMs);
    r.motorAmps = -0.6f;
    r.nowMs += 20;
    b.roof.tick(r);
    TEST_ASSERT_TRUE(b.roof.command(Command::Open, Origin::Person).reason == Refusal::CurrentUnreadable);
    r.motorAmps = -0.5f; // within the tolerance
    r.nowMs += 20;
    b.roof.tick(r);
    TEST_ASSERT_TRUE(b.roof.command(Command::Open, Origin::Person).accepted);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_dead_time_is_timed_by_steps_only);
    RUN_TEST(test_the_first_drive_after_start_up_waits_the_dead_time);
    RUN_TEST(test_repeated_restarts_hit_the_drive_budget);
    RUN_TEST(test_reaching_a_magnet_resets_the_drive_budget);
    RUN_TEST(test_a_move_after_25_days_idle_drives);
    RUN_TEST(test_a_move_after_48_days_idle_drives);
    RUN_TEST(test_idle_gaps_around_the_clock_wraps_never_block_a_move);
    RUN_TEST(test_after_a_long_idle_the_timeout_counts_from_this_drive);
    RUN_TEST(test_after_a_long_idle_the_leave_time_counts_from_this_drive);
    RUN_TEST(test_after_a_long_idle_a_confirm_window_starts_afresh);
    RUN_TEST(test_after_a_long_idle_a_stuck_condition_still_gets_its_confirm_time);
    RUN_TEST(test_after_a_long_idle_a_late_step_is_still_caught);
    RUN_TEST(test_a_reversal_near_the_end_never_trips_the_drive_budget);
    RUN_TEST(test_the_budget_still_trips_restarts_in_one_direction);
    RUN_TEST(test_a_step_clock_going_backwards_keeps_the_driver_off);
    RUN_TEST(test_abort_at_rest_keeps_a_known_position);
    RUN_TEST(test_implausible_supply_readings_are_unreadable);
    RUN_TEST(test_negative_current_is_unreadable_with_current_sense_on);
    return UNITY_END();
}
