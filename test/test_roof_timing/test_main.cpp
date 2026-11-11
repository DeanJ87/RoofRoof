// The roof controller's clocks and limits (spec 002 FR-002, FR-008, FR-025,
// FR-026; added after the safety review): the dead time timed by control steps
// only and applied after start-up, the drive budget across restarts, abort at
// rest, and readings too implausible to believe.

#include <cmath>
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
    RUN_TEST(test_abort_at_rest_keeps_a_known_position);
    RUN_TEST(test_implausible_supply_readings_are_unreadable);
    RUN_TEST(test_negative_current_is_unreadable_with_current_sense_on);
    return UNITY_END();
}
