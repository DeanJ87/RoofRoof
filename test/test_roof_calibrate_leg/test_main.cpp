// Spec 016: one leg of a calibration run in the roof controller (lib/RoofLogic,
// RoofControllerCalibrate.cpp, research R1): its refusals, its direction and
// duty profile, its end at the first magnet after leaving the start one, its
// own limit as timeout and budget, the existing faults, and its report.

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

    CalibrationLeg leg(End target, uint8_t duty = 40, uint32_t limitMs = 180000, HallLevel expect = HallLevel::Unsettled)
    {
        CalibrationLeg l;
        l.target = target;
        l.dutyPercent = duty;
        l.limitMs = limitMs;
        l.expect = expect;
        return l;
    }

    bool atRest(const Controller &c)
    {
        return c.status().state != State::Opening && c.status().state != State::Closing;
    }

    void assertRefused(Refusal reason, const CommandResult &result)
    {
        TEST_ASSERT_FALSE(result.accepted);
        TEST_ASSERT_EQUAL_STRING(refusalName(reason), refusalName(result.reason));
    }
} // namespace

// --- Refusals -------------------------------------------------------------------

void test_only_a_person_may_start_a_leg(void)
{
    Bench b;
    assertRefused(Refusal::NotAPerson, b.roof.calibrate(leg(End::Open), Origin::Client));
    assertRefused(Refusal::NotAPerson, b.roof.calibrate(leg(End::Open), Origin::Mqtt));
    assertRefused(Refusal::NotAPerson, b.roof.calibrate(leg(End::Open), Origin::Automatic));
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_a_leg_is_refused_with_a_fault_latched(void)
{
    Bench b;
    b.roof.raiseFault(Fault{FaultKind::External, 0, 0});
    assertRefused(Refusal::FaultLatched, b.roof.calibrate(leg(End::Open), Origin::Person));
}

void test_a_leg_is_refused_while_a_move_is_active(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    assertRefused(Refusal::Moving, b.roof.calibrate(leg(End::Open), Origin::Person));
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted == false);
}

void test_a_leg_needs_the_readiness_every_move_needs(void)
{
    Bench b;
    b.sim.supplyVolts = 10.0f;
    b.steps(2);
    assertRefused(Refusal::SupplyLow, b.roof.calibrate(leg(End::Open), Origin::Person));
}

void test_a_leg_outside_its_ranges_is_refused(void)
{
    Bench b;
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(leg(End::Open, 40, MIN_CALIBRATION_LEG_MS - 1), Origin::Person));
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(leg(End::Open, 40, MAX_CALIBRATION_LEG_MS + 1), Origin::Person));
    TEST_ASSERT_TRUE(b.roof.checkCalibrate(leg(End::Open, 40, MIN_CALIBRATION_LEG_MS), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.roof.checkCalibrate(leg(End::Open, 40, MAX_CALIBRATION_LEG_MS), Origin::Person).accepted);
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(leg(End::Open, 39), Origin::Person));
    Settings s;
    s.topDutyPercent = 80;
    b.roof.applySettings(s);
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(leg(End::Open, 81), Origin::Person));
    TEST_ASSERT_TRUE(b.roof.checkCalibrate(leg(End::Open, 80), Origin::Person).accepted);
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(leg(End::Open, 40, 180000, HallLevel::Rail), Origin::Person));
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(leg(End::Open, 40, 180000, HallLevel::Between), Origin::Person));
    CalibrationLeg bad = leg(End::Open);
    bad.target = static_cast<End>(7);
    assertRefused(Refusal::LegInvalid, b.roof.checkCalibrate(bad, Origin::Person));
}

void test_above_the_slow_duty_only_from_a_magnet(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    assertRefused(Refusal::PositionUnknown, b.roof.checkCalibrate(leg(End::Open, 41), Origin::Person));
    TEST_ASSERT_TRUE(b.roof.checkCalibrate(leg(End::Open, 40), Origin::Person).accepted);
    Bench closed;
    TEST_ASSERT_TRUE(closed.roof.checkCalibrate(leg(End::Open, 100), Origin::Person).accepted);
}

// --- Direction, duty and the end -------------------------------------------------

void test_a_slow_leg_drives_toward_its_end_after_the_dead_time(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Opening);
    TEST_ASSERT_TRUE(b.roof.status().calibrating);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    const Output out = b.step();
    TEST_ASSERT_TRUE(out.command == Driver::Command::Forward);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, out.duty);
    TEST_ASSERT_EQUAL_UINT32(180000, b.roof.status().moveTimeoutMs);
}

void test_the_dead_time_runs_before_a_leg_after_a_drive(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(10);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Closed), Origin::Person).accepted);
    uint32_t offMs = 0;
    while (b.step().command == Driver::Command::Off)
        offMs += b.stepMs;
    TEST_ASSERT_TRUE(offMs + b.stepMs >= DEAD_TIME_MS);
}

void test_reverse_open_direction_drives_the_other_output(void)
{
    Settings s;
    s.openDirection = Driver::Command::Reverse;
    Bench b(s);
    b.sim.reversedWiring = true;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.step().command == Driver::Command::Reverse);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_a_slow_leg_stops_at_the_other_magnet_and_reports_it(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_FALSE(b.roof.status().calibrating);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    const CalibrationReport &r = b.roof.calibrationReport();
    TEST_ASSERT_EQUAL_UINT32(1, r.ended);
    TEST_ASSERT_TRUE(r.arrived);
    TEST_ASSERT_TRUE(r.target == End::Open);
    TEST_ASSERT_TRUE(r.startLevel == HallLevel::Low);
    TEST_ASSERT_TRUE(r.level == HallLevel::High);
    // 1825 mm (the magnet zones are 25 mm) at 38 mm/s x 0.4: about 120 s.
    const float expectedMs = 1825.0f / (38.0f * (SLOW_DUTY / 255.0f)) * 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(200.0f, expectedMs, static_cast<float>(r.drivenMs));
    TEST_ASSERT_FLOAT_WITHIN(0.01f * r.drivenMs, r.drivenMs * (SLOW_DUTY / 255.0f), r.dutyMs);
}

void test_a_leg_ends_at_the_next_magnet_whatever_the_polarity_setting(void)
{
    // The setting says the open end reads high; the roof's closed end does.
    Bench b;
    b.sim.highEndClosed = true;
    b.steps(10);
    TEST_ASSERT_TRUE(b.state() == State::Open); // the saved settings' label
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    const CalibrationReport &r = b.roof.calibrationReport();
    TEST_ASSERT_TRUE(r.arrived);
    TEST_ASSERT_TRUE(r.startLevel == HallLevel::High);
    TEST_ASSERT_TRUE(r.level == HallLevel::Low);
    TEST_ASSERT_TRUE(b.sim.positionMm > 1800.0f);
}

void test_a_leg_with_an_expected_level_faults_on_the_other(void)
{
    Bench b(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Closed, 40, 180000, HallLevel::High), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Open), b.roof.status().fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Closed), b.roof.status().fault.limit);
    TEST_ASSERT_FALSE(b.roof.calibrationReport().arrived);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_a_leg_with_the_expected_level_arrives(void)
{
    Bench b(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Closed, 40, 180000, HallLevel::Low), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.calibrationReport().arrived);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_the_start_magnet_again_after_leaving_it_is_an_arrival_when_any_level_will_do(void)
{
    // Both magnets read high (the same pole): the leg still stops at the next one.
    Bench b;
    b.sim.lowMagnetCounts = 3400;
    b.steps(10);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.calibrationReport().arrived);
    TEST_ASSERT_TRUE(b.roof.calibrationReport().startLevel == HallLevel::High);
    TEST_ASSERT_TRUE(b.roof.calibrationReport().level == HallLevel::High);
    TEST_ASSERT_TRUE(b.sim.positionMm > 1800.0f);
}

void test_a_leg_from_part_way_stops_at_the_first_magnet(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Closed), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.roof.status().fromUnknown);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.calibrationReport().arrived);
    TEST_ASSERT_TRUE(b.roof.calibrationReport().startLevel == HallLevel::Between);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

// --- The top-duty profile -------------------------------------------------------------

void test_leg_fraction_ramps_up_and_back_down(void)
{
    Settings s; // ramp up 2000 ms, down 3000 ms, slow 40 %
    CalibrationLeg l = leg(End::Open, 100);
    l.slowAfterMs = 20000;
    TEST_ASSERT_EQUAL_FLOAT(0.4f, legFraction(s, l, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.7f, legFraction(s, l, 1000));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, legFraction(s, l, 2000));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, legFraction(s, l, 19999));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, legFraction(s, l, 20000));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.7f, legFraction(s, l, 21500));
    TEST_ASSERT_EQUAL_FLOAT(0.4f, legFraction(s, l, 23000));
    TEST_ASSERT_EQUAL_FLOAT(0.4f, legFraction(s, l, 90000));
    l.slowAfterMs = 0;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, legFraction(s, l, 90000));
    TEST_ASSERT_EQUAL_FLOAT(0.4f, legFraction(s, leg(End::Open, 40), 90000));
    s.rampUpMs = 0;
    s.rampDownMs = 0;
    l.slowAfterMs = 5000;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, legFraction(s, l, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.4f, legFraction(s, l, 5000));
}

void test_a_top_duty_leg_follows_its_profile(void)
{
    Bench b;
    CalibrationLeg l = leg(End::Open, 100);
    l.slowAfterMs = 20000;
    TEST_ASSERT_TRUE(b.roof.calibrate(l, Origin::Person).accepted);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.step().duty);
    b.steps(150); // 3 s
    TEST_ASSERT_EQUAL_UINT8(TOP_DUTY, b.roof.output().duty);
    b.steps(1000); // 23 s
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.roof.output().duty);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.calibrationReport().arrived);
}

// --- Its limit and the existing faults -----------------------------------------------

void test_a_leg_that_reaches_no_magnet_times_out_at_its_limit(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.openMagnetMissing = true;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open, 40, 30000), Origin::Person).accepted);
    b.step(); // the driver turns on
    TEST_ASSERT_TRUE(b.roof.status().driving);
    TEST_ASSERT_FALSE(b.runUntil([](const Controller &c) { return !c.status().driving; }, 30000 - 2 * b.stepMs));
    TEST_ASSERT_TRUE(b.runUntil([](const Controller &c) { return !c.status().driving; }, 4 * b.stepMs));
    const Fault &f = b.roof.status().fault;
    TEST_ASSERT_TRUE(f.kind == FaultKind::Timeout);
    TEST_ASSERT_EQUAL_FLOAT(30000.0f, f.limit);
    TEST_ASSERT_TRUE(f.measured >= 30000.0f);
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_FALSE(b.roof.calibrationReport().arrived);
}

void test_a_leg_that_doesnt_leave_its_magnet_faults_did_not_leave(void)
{
    Bench b;
    b.sim.jammed = true;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest(20000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_EQUAL_FLOAT(10000.0f, b.roof.status().fault.limit);
}

void test_the_budget_is_the_legs_limit_and_counts_across_legs(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.jammed = true; // between the magnets: nothing to leave, nothing reached
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open, 40, 20000), Origin::Person).accepted);
    b.steps(1 + 15000 / 20);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open, 40, 20000), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest(10000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::DriveBudget);
    TEST_ASSERT_EQUAL_FLOAT(20000.0f, b.roof.status().fault.limit);
}

void test_a_low_supply_during_a_leg_faults(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(100);
    b.sim.supplyVolts = 10.0f;
    TEST_ASSERT_TRUE(b.runToRest(1000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::SupplyLow);
}

void test_over_current_during_a_leg_faults(void)
{
    Settings s;
    s.currentSenseEnabled = true;
    Bench b(s, 900.0f);
    b.sim.jammed = true;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.runToRest(1000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::OverCurrent);
}

void test_a_hall_rail_during_a_leg_faults(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(100);
    b.sim.railCounts = 0;
    TEST_ASSERT_TRUE(b.runToRest(1000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_FALSE(b.roof.status().calibrating);
}

void test_a_late_step_during_a_leg_faults(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(10);
    b.stepMs = MAX_STEP_GAP_MS + 20;
    b.step();
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::LateStep);
}

// --- Stops, other commands, the estimate ---------------------------------------------

void test_a_stop_ends_a_leg_and_keeps_the_estimate_from_a_magnet(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(1 + 5000 / 20);
    b.command(Command::Stop);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, b.sim.positionMm, b.roof.status().positionMm);
    const CalibrationReport &r = b.roof.calibrationReport();
    TEST_ASSERT_FALSE(r.arrived);
    TEST_ASSERT_EQUAL_UINT32(1, r.ended);
    TEST_ASSERT_UINT32_WITHIN(40, 5000, r.drivenMs);
    TEST_ASSERT_FALSE(b.roof.status().calibrating);
}

void test_a_stop_from_part_way_leaves_the_position_unknown(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(50);
    b.command(Command::Abort);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
}

void test_a_command_to_the_same_end_leaves_the_leg_running(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(10);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.roof.status().calibrating);
    TEST_ASSERT_EQUAL_UINT32(0, b.roof.calibrationReport().ended);
}

void test_a_reversal_replaces_the_leg_and_ends_it(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    b.steps(100);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_EQUAL_UINT32(1, b.roof.calibrationReport().ended);
    TEST_ASSERT_FALSE(b.roof.calibrationReport().arrived);
    TEST_ASSERT_FALSE(b.roof.status().calibrating);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_settings_are_refused_during_a_leg(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg(End::Open), Origin::Person).accepted);
    assertRefused(Refusal::Moving, b.roof.applySettings(Settings()));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_a_person_may_start_a_leg);
    RUN_TEST(test_a_leg_is_refused_with_a_fault_latched);
    RUN_TEST(test_a_leg_is_refused_while_a_move_is_active);
    RUN_TEST(test_a_leg_needs_the_readiness_every_move_needs);
    RUN_TEST(test_a_leg_outside_its_ranges_is_refused);
    RUN_TEST(test_above_the_slow_duty_only_from_a_magnet);
    RUN_TEST(test_a_slow_leg_drives_toward_its_end_after_the_dead_time);
    RUN_TEST(test_the_dead_time_runs_before_a_leg_after_a_drive);
    RUN_TEST(test_reverse_open_direction_drives_the_other_output);
    RUN_TEST(test_a_slow_leg_stops_at_the_other_magnet_and_reports_it);
    RUN_TEST(test_a_leg_ends_at_the_next_magnet_whatever_the_polarity_setting);
    RUN_TEST(test_a_leg_with_an_expected_level_faults_on_the_other);
    RUN_TEST(test_a_leg_with_the_expected_level_arrives);
    RUN_TEST(test_the_start_magnet_again_after_leaving_it_is_an_arrival_when_any_level_will_do);
    RUN_TEST(test_a_leg_from_part_way_stops_at_the_first_magnet);
    RUN_TEST(test_leg_fraction_ramps_up_and_back_down);
    RUN_TEST(test_a_top_duty_leg_follows_its_profile);
    RUN_TEST(test_a_leg_that_reaches_no_magnet_times_out_at_its_limit);
    RUN_TEST(test_a_leg_that_doesnt_leave_its_magnet_faults_did_not_leave);
    RUN_TEST(test_the_budget_is_the_legs_limit_and_counts_across_legs);
    RUN_TEST(test_a_low_supply_during_a_leg_faults);
    RUN_TEST(test_over_current_during_a_leg_faults);
    RUN_TEST(test_a_hall_rail_during_a_leg_faults);
    RUN_TEST(test_a_late_step_during_a_leg_faults);
    RUN_TEST(test_a_stop_ends_a_leg_and_keeps_the_estimate_from_a_magnet);
    RUN_TEST(test_a_stop_from_part_way_leaves_the_position_unknown);
    RUN_TEST(test_a_command_to_the_same_end_leaves_the_leg_running);
    RUN_TEST(test_a_reversal_replaces_the_leg_and_ends_it);
    RUN_TEST(test_settings_are_refused_during_a_leg);
    return UNITY_END();
}
