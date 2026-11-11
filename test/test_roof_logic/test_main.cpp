// The roof controller's moves (spec 002 US1, US3, US4; lib/RoofLogic): open and
// close end to end, never moving by itself from an unknown position, stop,
// abort, reversal and settings. Faults are in test_roof_faults; the random
// sequences in test_roof_properties.

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
    const uint8_t SLOW_DUTY = toDuty(0.40f); // 102
    const uint8_t TOP_DUTY = toDuty(1.0f);   // 255

    bool isOpen(const Controller &c)
    {
        return c.status().state == State::Open;
    }

    // Opens a closed bench roof part-way: `ms` of driving.
    void openFor(Bench &b, uint32_t ms)
    {
        TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
        for (uint32_t t = 0; t < ms; t += b.stepMs)
            b.step();
    }
} // namespace

// --- US1: open and close end to end -------------------------------------------

void test_boot_at_closed_magnet_is_closed_and_off(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.roof.status().positionMm);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_boot_at_open_magnet_is_open(void)
{
    Bench b(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_EQUAL_FLOAT(1850.0f, b.roof.status().positionMm);
}

void test_hall_polarity_setting_swaps_the_ends(void)
{
    Settings s;
    s.hallHighEnd = End::Closed;
    Bench b(s);
    b.sim.highEndClosed = true;
    b.settleHall();
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_open_drives_the_open_direction_from_the_slow_duty(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Opening);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off); // the drive starts on the next step
    const Output out = b.step();
    TEST_ASSERT_TRUE(out.command == Driver::Command::Forward);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, out.duty);
    TEST_ASSERT_TRUE(b.roof.status().driving);
    TEST_ASSERT_TRUE(b.roof.status().target == End::Open);
}

void test_open_direction_setting_reverses_the_driver(void)
{
    Settings s;
    s.openDirection = Driver::Command::Reverse;
    Bench b(s);
    b.sim.reversedWiring = true; // the motor is wired so reverse opens
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Reverse);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off); // dead time first
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return c.output().command != Driver::Command::Off; }, 1000));
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Forward);
}

void test_ramp_up_reaches_the_top_duty_after_the_ramp_time(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.step();           // driven from here
    b.steps(1000 / 20); // 1000 ms in: halfway, 70%
    TEST_ASSERT_UINT8_WITHIN(1, toDuty(0.7f), b.roof.output().duty);
    b.steps(1000 / 20); // 2000 ms in: the top duty
    TEST_ASSERT_EQUAL_UINT8(TOP_DUTY, b.roof.output().duty);
    for (int i = 0; i < 100; ++i) // and it stays there mid-travel
        TEST_ASSERT_EQUAL_UINT8(TOP_DUTY, b.step().duty);
}

// FR-011: Off on the step the target magnet is confirmed, and every step after.
void test_open_stops_on_the_open_magnet(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().hall == HallLevel::High);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_EQUAL_FLOAT(1850.0f, b.roof.status().positionMm);
    TEST_ASSERT_TRUE(b.sim.atOpenMagnet());
    const float parked = b.sim.positionMm;
    b.steps(500);
    TEST_ASSERT_EQUAL_FLOAT(parked, b.sim.positionMm);
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_close_stops_on_the_closed_magnet(void)
{
    Bench b(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Reverse);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.roof.status().positionMm);
    TEST_ASSERT_TRUE(b.sim.atClosedMagnet());
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

// FR-010: in the last stretch the duty has fallen back to near the slow duty.
void test_slows_down_near_the_end(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    uint8_t lastDuties[10] = {};
    int count = 0;
    while (b.state() == State::Opening && count < 100000)
    {
        lastDuties[count % 10] = b.step().duty;
        ++count;
    }
    TEST_ASSERT_TRUE(b.state() == State::Open);
    // The final driven steps (the arrival step itself is Off).
    for (int i = 2; i <= 9; ++i)
        TEST_ASSERT_TRUE(lastDuties[(count - i) % 10] < 150);
}

void test_open_when_open_is_an_accepted_no_op(void)
{
    Bench b(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    Bench c;
    TEST_ASSERT_TRUE(c.command(Command::Close, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(c.state() == State::Closed);
    TEST_ASSERT_TRUE(c.step().command == Driver::Command::Off);
}

void test_a_repeated_command_leaves_the_move_unchanged(void)
{
    Bench b;
    openFor(b, 3000);
    const uint32_t elapsed = b.roof.status().moveElapsedMs;
    const uint32_t timeout = b.roof.status().moveTimeoutMs;
    TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Automatic).accepted);
    const Output out = b.step();
    TEST_ASSERT_TRUE(out.command == Driver::Command::Forward);
    TEST_ASSERT_EQUAL_UINT32(elapsed + 20, b.roof.status().moveElapsedMs);
    TEST_ASSERT_EQUAL_UINT32(timeout, b.roof.status().moveTimeoutMs);
}

// SC-003: both moves end at their magnet within the timeout, and the timeout is
// the profile's expected time plus the margin.
void test_end_to_end_open_and_close_within_their_timeouts(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.step();
    TEST_ASSERT_EQUAL_UINT32(moveTimeoutMs(Settings(), 1850.0f), b.roof.status().moveTimeoutMs);
    uint32_t driven = 0;
    while (b.state() == State::Opening)
    {
        driven = b.roof.status().moveElapsedMs;
        b.step();
    }
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(driven < moveTimeoutMs(Settings(), 1850.0f));
    TEST_ASSERT_TRUE(driven > 45000);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

// A roof 10% slower than its setting still ends at the magnet: it creeps at the
// slow duty after the estimate arrives, inside the margin.
void test_a_slower_roof_creeps_to_the_magnet(void)
{
    Bench b;
    b.sim.speedMmPerSecond = 34.2f;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

// --- US3: never by itself from an unknown position ----------------------------

void test_boot_between_magnets_is_unknown_and_off(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
    for (int i = 0; i < 3000; ++i)
        TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, b.sim.positionMm);
}

void test_commands_refused_until_the_hall_settles(void)
{
    Controller roof;
    TEST_ASSERT_TRUE(roof.status().state == State::Unknown);
    CommandResult r = roof.command(Command::Open, Origin::Person);
    TEST_ASSERT_FALSE(r.accepted);
    TEST_ASSERT_TRUE(r.reason == Refusal::HallNotSettled);
    Readings readings;
    readings.hallCounts = 700;
    readings.supplyVolts = 12.5f;
    for (uint32_t i = 1; i <= 2; ++i)
    {
        readings.nowMs = i * 20;
        roof.tick(readings);
    }
    TEST_ASSERT_TRUE(roof.command(Command::Close, Origin::Person).reason == Refusal::HallNotSettled);
    readings.nowMs = 60;
    roof.tick(readings);
    TEST_ASSERT_TRUE(roof.status().state == State::Closed);
}

void test_automatic_moves_refused_when_unknown(void)
{
    Bench b(Settings(), 900.0f);
    for (Command c : {Command::Open, Command::Close})
    {
        const CommandResult r = b.command(c, Origin::Automatic);
        TEST_ASSERT_FALSE(r.accepted);
        TEST_ASSERT_TRUE(r.reason == Refusal::PositionUnknown);
    }
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
}

void test_person_move_from_unknown_is_slow_and_stops_at_the_first_magnet(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_TRUE(b.roof.status().fromUnknown);
    b.step();
    TEST_ASSERT_EQUAL_UINT32(unknownMoveTimeoutMs(Settings()), b.roof.status().moveTimeoutMs);
    int driven = 0;
    while (b.state() == State::Closing && driven < 100000)
    {
        const Output out = b.step();
        TEST_ASSERT_TRUE(out.duty <= SLOW_DUTY);
        TEST_ASSERT_FALSE(b.roof.status().positionKnown && b.state() == State::Closing);
        ++driven;
    }
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_FALSE(b.roof.status().fromUnknown);
    TEST_ASSERT_TRUE(b.sim.atClosedMagnet());
}

void test_person_move_from_unknown_to_the_wrong_magnet_faults(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.reversedWiring = true; // "close" actually drives toward open
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Open), b.roof.status().fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Closed), b.roof.status().fault.limit);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_abort_part_way_makes_the_position_unknown(void)
{
    Bench b;
    openFor(b, 5000);
    TEST_ASSERT_TRUE(b.command(Command::Abort, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off); // at once, not on the next step
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).reason == Refusal::PositionUnknown);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
}

void test_abort_before_leaving_a_magnet_keeps_the_end(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.step();
    TEST_ASSERT_TRUE(b.command(Command::Abort).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
}

void test_leaving_a_magnet_at_rest_makes_the_position_unknown(void)
{
    Bench b;
    b.sim.positionMm = 100.0f; // moved by hand, or the sensor drifted
    b.steps(3);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
}

void test_a_magnet_read_at_rest_makes_the_position_known(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.positionMm = 1850.0f;
    b.steps(3);
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
}

void test_supply_low_at_rest_refuses_without_latching(void)
{
    Bench b;
    b.sim.supplyVolts = 10.9f;
    b.step();
    CommandResult r = b.command(Command::Open);
    TEST_ASSERT_FALSE(r.accepted);
    TEST_ASSERT_TRUE(r.reason == Refusal::SupplyLow);
    TEST_ASSERT_EQUAL_FLOAT(10.9f, r.measured);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, r.limit);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    b.sim.supplyVolts = NAN;
    b.step();
    r = b.command(Command::Open);
    TEST_ASSERT_TRUE(r.reason == Refusal::SupplyUnreadable);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, r.limit);
    b.sim.supplyVolts = 11.0f; // at the minimum is enough
    b.step();
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
}

void test_current_unreadable_refuses_only_when_current_sense_is_on(void)
{
    Bench off;
    off.sim.currentFitted = false;
    off.step();
    TEST_ASSERT_TRUE(off.command(Command::Open).accepted);

    Settings s;
    s.currentSenseEnabled = true;
    Bench on(s);
    on.sim.currentFitted = false;
    on.step();
    const CommandResult r = on.command(Command::Open);
    TEST_ASSERT_TRUE(r.reason == Refusal::CurrentUnreadable);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, r.limit);
}

// --- US4: stop part-way, carry on, reverse -------------------------------------

void test_stop_part_way_keeps_the_estimate(void)
{
    Bench b;
    openFor(b, 10000);
    TEST_ASSERT_TRUE(b.command(Command::Stop, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_FLOAT_WITHIN(b.sim.positionMm * 0.02f, b.sim.positionMm, b.roof.status().positionMm);
    b.steps(100);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
}

void test_resume_from_stopped_in_either_direction(void)
{
    Bench b;
    openFor(b, 10000);
    b.command(Command::Stop);
    const float estimate = b.roof.status().positionMm;
    // An automatic close may carry on from a known estimate (spec 005's close).
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return c.status().driving; }, 2000));
    TEST_ASSERT_EQUAL_UINT32(moveTimeoutMs(Settings(), estimate), b.roof.status().moveTimeoutMs);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);

    openFor(b, 8000);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

// constitution I: a direction change goes through Off for the dead time.
void test_reversal_waits_the_dead_time_off(void)
{
    Bench b;
    openFor(b, 5000);
    const uint32_t reversedAt = b.nowMs;
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    // The dead time is timed from the first step after the stop.
    const uint32_t firstStepMs = reversedAt + b.stepMs;
    while (b.nowMs + b.stepMs - firstStepMs < DEAD_TIME_MS)
        TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Reverse);
    TEST_ASSERT_EQUAL_UINT32(DEAD_TIME_MS, b.nowMs - firstStepMs);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_a_restart_after_stop_waits_the_dead_time(void)
{
    Bench b;
    openFor(b, 3000);
    b.command(Command::Stop);
    const uint32_t stoppedAt = b.nowMs;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    const uint32_t firstStepMs = stoppedAt + b.stepMs;
    while (b.nowMs + b.stepMs - firstStepMs < DEAD_TIME_MS)
        TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Forward);
}

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

void test_stop_during_the_dead_time_returns_to_the_previous_state(void)
{
    Bench b;
    openFor(b, 3000);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.step();
    TEST_ASSERT_TRUE(b.command(Command::Stop).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);

    Bench c;
    TEST_ASSERT_TRUE(c.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(c.command(Command::Stop).accepted); // before any step drove it
    TEST_ASSERT_TRUE(c.state() == State::Closed);
}

void test_abort_during_the_dead_time_part_way_makes_the_position_unknown(void)
{
    Bench b;
    openFor(b, 3000);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    b.step();
    TEST_ASSERT_TRUE(b.command(Command::Abort).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
}

// Current is only checked while the driver is on: not during the dead time.
void test_current_is_not_checked_during_the_dead_time(void)
{
    Settings s;
    s.currentSenseEnabled = true;
    Bench b(s);
    openFor(b, 3000);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    Readings r = b.sim.readings(b.nowMs);
    r.motorAmps = 50.0f;
    for (int i = 0; i < 24; ++i) // 480 ms, all inside the dead time
    {
        r.nowMs += 20;
        TEST_ASSERT_TRUE(b.roof.tick(r).command == Driver::Command::Off);
    }
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

void test_settings_refused_while_moving(void)
{
    Bench b;
    openFor(b, 3000);
    Settings changed;
    changed.travelMm = 1000;
    TEST_ASSERT_TRUE(b.roof.applySettings(changed).reason == Refusal::Moving);
    TEST_ASSERT_EQUAL_UINT32(1850, b.roof.settings().travelMm);
    // Even while waiting out the dead time a move is active.
    b.command(Command::Close);
    TEST_ASSERT_TRUE(b.roof.applySettings(changed).reason == Refusal::Moving);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.roof.applySettings(changed).accepted);
    TEST_ASSERT_EQUAL_UINT32(1000, b.roof.settings().travelMm);
    // The estimate was made with the old settings.
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
}

void test_new_settings_at_an_end_keep_it_known(void)
{
    Bench b(Settings(), 1850.0f);
    Settings changed;
    changed.travelMm = 1700;
    TEST_ASSERT_TRUE(b.roof.applySettings(changed).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_EQUAL_FLOAT(1700.0f, b.roof.status().positionMm);
}

void test_invalid_settings_refuse_every_move(void)
{
    Settings bad;
    bad.speedMmPerSecond = 0.0f;
    Bench b(bad);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.command(Command::Open).reason == Refusal::SettingsInvalid);
    TEST_ASSERT_TRUE(b.roof.applySettings(Settings()).accepted);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
}

void test_the_clock_wrapping_mid_move_is_harmless(void)
{
    Bench b;
    b.nowMs = 0xFFFFFFFFu - 20000u;
    b.settleHall();
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.nowMs < 100000u); // it did wrap
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_at_closed_magnet_is_closed_and_off);
    RUN_TEST(test_boot_at_open_magnet_is_open);
    RUN_TEST(test_hall_polarity_setting_swaps_the_ends);
    RUN_TEST(test_open_drives_the_open_direction_from_the_slow_duty);
    RUN_TEST(test_open_direction_setting_reverses_the_driver);
    RUN_TEST(test_ramp_up_reaches_the_top_duty_after_the_ramp_time);
    RUN_TEST(test_open_stops_on_the_open_magnet);
    RUN_TEST(test_close_stops_on_the_closed_magnet);
    RUN_TEST(test_slows_down_near_the_end);
    RUN_TEST(test_open_when_open_is_an_accepted_no_op);
    RUN_TEST(test_a_repeated_command_leaves_the_move_unchanged);
    RUN_TEST(test_end_to_end_open_and_close_within_their_timeouts);
    RUN_TEST(test_a_slower_roof_creeps_to_the_magnet);
    RUN_TEST(test_boot_between_magnets_is_unknown_and_off);
    RUN_TEST(test_commands_refused_until_the_hall_settles);
    RUN_TEST(test_automatic_moves_refused_when_unknown);
    RUN_TEST(test_person_move_from_unknown_is_slow_and_stops_at_the_first_magnet);
    RUN_TEST(test_person_move_from_unknown_to_the_wrong_magnet_faults);
    RUN_TEST(test_abort_part_way_makes_the_position_unknown);
    RUN_TEST(test_abort_before_leaving_a_magnet_keeps_the_end);
    RUN_TEST(test_leaving_a_magnet_at_rest_makes_the_position_unknown);
    RUN_TEST(test_a_magnet_read_at_rest_makes_the_position_known);
    RUN_TEST(test_supply_low_at_rest_refuses_without_latching);
    RUN_TEST(test_current_unreadable_refuses_only_when_current_sense_is_on);
    RUN_TEST(test_stop_part_way_keeps_the_estimate);
    RUN_TEST(test_resume_from_stopped_in_either_direction);
    RUN_TEST(test_reversal_waits_the_dead_time_off);
    RUN_TEST(test_a_restart_after_stop_waits_the_dead_time);
    RUN_TEST(test_dead_time_is_timed_by_steps_only);
    RUN_TEST(test_the_first_drive_after_start_up_waits_the_dead_time);
    RUN_TEST(test_repeated_restarts_hit_the_drive_budget);
    RUN_TEST(test_reaching_a_magnet_resets_the_drive_budget);
    RUN_TEST(test_abort_at_rest_keeps_a_known_position);
    RUN_TEST(test_implausible_supply_readings_are_unreadable);
    RUN_TEST(test_negative_current_is_unreadable_with_current_sense_on);
    RUN_TEST(test_stop_during_the_dead_time_returns_to_the_previous_state);
    RUN_TEST(test_abort_during_the_dead_time_part_way_makes_the_position_unknown);
    RUN_TEST(test_current_is_not_checked_during_the_dead_time);
    RUN_TEST(test_settings_refused_while_moving);
    RUN_TEST(test_new_settings_at_an_end_keep_it_known);
    RUN_TEST(test_invalid_settings_refuse_every_move);
    RUN_TEST(test_the_clock_wrapping_mid_move_is_harmless);
    return UNITY_END();
}
