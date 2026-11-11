// Spec 016: the calibration run (lib/CalibrationLogic, CalibrationRun) end to
// end against the simulated roof, stepped every 20 ms with the firmware's
// order: the gates (the park interlock, the open gate), the controller's tick,
// then the run. Covers the start refusals, the legs and their limits, the
// pauses, every ending, the plant's faults and the proposal it leads to.

#include <cmath>
#include <unity.h>

#include "CalibrationRun.h"
#include "MotionProfile.h"
#include "RoofController.h"
#include "RoofGates.h"
#include "RoofPlant.h"
#include "RoofText.h"

#include "../calibration_rig/CalibrationRig.h"

using namespace CalibTest;

using namespace SQM;
using namespace SQM::Calib;
using Roof::End;
using Roof::FaultKind;
using Roof::HallLevel;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

// --- Full runs ------------------------------------------------------------------------

void test_a_run_from_closed_measures_and_proposes(void)
{
    Rig r;
    r.plant.speedMmPerSecond = 41.0f; // the real roof, faster than the setting
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.run.active());
    TEST_ASSERT_TRUE(r.finish());
    const Report &rep = r.rep();
    TEST_ASSERT_TRUE(rep.state == RunState::Done);
    TEST_ASSERT_TRUE(rep.ending == Ending::Done);
    TEST_ASSERT_EQUAL_UINT8(2, rep.measured.legCount);
    TEST_ASSERT_TRUE(rep.measured.legs[0].kind == LegKind::SlowOpen);
    TEST_ASSERT_TRUE(rep.measured.legs[1].kind == LegKind::SlowClose);
    TEST_ASSERT_TRUE(rep.proposed);
    const Proposal &p = rep.proposal;
    TEST_ASSERT_TRUE(p.speed);
    TEST_ASSERT_FLOAT_WITHIN(41.0f * 0.02f, 41.0f, p.speedMmPerSecond);
    TEST_ASSERT_TRUE(p.timeoutMarginSeconds >= 5);
    TEST_ASSERT_TRUE(p.hall);
    TEST_ASSERT_TRUE(p.hallHighEnd == End::Open);
    TEST_ASSERT_EQUAL_UINT16(3000, p.hallHighCounts);
    TEST_ASSERT_EQUAL_UINT16(1100, p.hallLowCounts);
    TEST_ASSERT_EQUAL_UINT32(1800, p.travelMm);
    // Nothing changed: the run only proposes.
    TEST_ASSERT_TRUE(sameSettings(r.settings, r.roof.settings()));
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
    TEST_ASSERT_TRUE(r.plant.positionMm < 30.0f);
    TEST_ASSERT_TRUE(rep.measured.closedRest.count >= SETTLE_MS / STEP_MS);
    TEST_ASSERT_TRUE(rep.measured.openRest.count >= SETTLE_MS / STEP_MS);
    TEST_ASSERT_EQUAL_UINT16(2050, rep.measured.between.min);
    TEST_ASSERT_EQUAL_UINT16(2050, rep.measured.between.max);
}

void test_a_run_from_part_way_closes_first(void)
{
    Rig r(900.0f);
    TEST_ASSERT_TRUE(r.start(Where::PartWay).accepted);
    TEST_ASSERT_EQUAL_UINT8(3, r.rep().legTotal);
    TEST_ASSERT_TRUE(r.rep().leg == LegKind::ToClosed);
    TEST_ASSERT_TRUE(r.until(LegKind::ToClosed));
    TEST_ASSERT_TRUE(r.roof.status().fromUnknown);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off || r.roof.output().duty == Roof::toDuty(0.4f));
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
    TEST_ASSERT_EQUAL_UINT8(2, r.rep().measured.legCount);
    TEST_ASSERT_TRUE(r.rep().measured.closedLevel == HallLevel::Low);
}

void test_a_run_from_open_closes_first(void)
{
    Rig r(1850.0f);
    TEST_ASSERT_TRUE(r.start(Where::Open).accepted);
    TEST_ASSERT_TRUE(r.rep().leg == LegKind::ToClosed);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
    TEST_ASSERT_TRUE(r.rep().proposal.hall);
}

void test_the_top_pass_measures_two_more_legs_at_the_top_duty(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start(Where::Closed, true).accepted);
    TEST_ASSERT_EQUAL_UINT8(4, r.rep().legTotal);
    TEST_ASSERT_TRUE(r.until(LegKind::TopOpen));
    r.steps(5000);
    TEST_ASSERT_EQUAL_UINT8(255, r.roof.output().duty);
    TEST_ASSERT_TRUE(r.finish());
    const Report &rep = r.rep();
    TEST_ASSERT_TRUE(rep.state == RunState::Done);
    TEST_ASSERT_EQUAL_UINT8(4, rep.measured.legCount);
    TEST_ASSERT_EQUAL_UINT8(100, rep.measured.legs[2].dutyPercent);
    TEST_ASSERT_FLOAT_WITHIN(38.0f * 0.02f, 38.0f, rep.proposal.speedMmPerSecond);
    // The top legs end at the slow duty, well inside their limits.
    TEST_ASSERT_TRUE(rep.measured.legs[2].drivenMs < rep.measured.legs[0].drivenMs);
}

void test_later_legs_are_bounded_by_the_measured_ones(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start(Where::Closed, true).accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    TEST_ASSERT_EQUAL_UINT32(LIMIT_MS, r.rep().legLimitMs);
    TEST_ASSERT_EQUAL_UINT32(LIMIT_MS, r.roof.status().moveTimeoutMs == 0 ? LIMIT_MS : r.roof.status().moveTimeoutMs);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose));
    const uint32_t open = r.rep().measured.legs[0].drivenMs;
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<uint32_t>(open * 1.5 + 5000) < LIMIT_MS ? static_cast<uint32_t>(open * 1.5 + 5000) : LIMIT_MS, r.rep().legLimitMs);
    TEST_ASSERT_TRUE(r.until(LegKind::TopOpen));
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(open * 1.25 + 5000), r.rep().legLimitMs);
    TEST_ASSERT_TRUE(r.until(LegKind::TopClose));
    const uint32_t close = r.rep().measured.legs[1].drivenMs;
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(close * 1.25 + 5000), r.rep().legLimitMs);
}

void test_each_end_waits_a_second_at_rest(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.rep().phase == Phase::Settle);
    r.steps(SETTLE_MS - 2 * STEP_MS);
    TEST_ASSERT_TRUE(r.rep().phase == Phase::Settle);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
    r.steps(3 * STEP_MS);
    TEST_ASSERT_TRUE(r.rep().phase == Phase::Drive);
}

// --- Start refusals --------------------------------------------------------------------

void test_a_start_needs_the_attended_confirmation(void)
{
    Rig r;
    Request req;
    req.at = Where::Closed;
    req.travelMm = 1800;
    assertRefused("notAttended", r.run.start(r.roof, req, r.startInputs()));
    TEST_ASSERT_FALSE(r.run.active());
}

void test_a_second_start_is_refused_while_running(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    assertRefused("calibrating", r.start());
}

void test_starts_are_refused_on_the_close_sequence_fault_and_motion(void)
{
    Rig r;
    Request req;
    req.attended = true;
    req.travelMm = 1800;
    StartInputs in = r.startInputs();
    in.sequenceIdle = false;
    assertRefused("closeSequence", r.run.start(r.roof, req, in));
    in = r.startInputs();
    in.closePending = true;
    assertRefused("closeWaiting", r.run.start(r.roof, req, in));
    in = r.startInputs();
    in.settingsPending = true;
    assertRefused("settingsPending", r.run.start(r.roof, req, in));
    TEST_ASSERT_TRUE(r.roof.command(Roof::Command::Open, Roof::Origin::Person).accepted);
    assertRefused("moving", r.run.start(r.roof, req, r.startInputs()));
    r.roof.command(Roof::Command::Stop, Roof::Origin::Person);
    r.roof.raiseFault(Roof::Fault{FaultKind::External, 0, 0});
    in = r.startInputs();
    in.faultText = "Watchdog";
    assertRefused("faultLatched", r.run.start(r.roof, req, in));
}

void test_starts_are_refused_unless_the_weather_is_safe_and_the_mount_parked(void)
{
    Rig r;
    r.gates.weather = Safety::Verdict::Unsafe;
    r.gates.weatherText = "unsafe: rain";
    assertRefused("weatherNotSafe", r.start());
    TEST_ASSERT_TRUE(r.start().text.find("rain") != std::string::npos);
    r.gates.weather = Safety::Verdict::Unknown;
    assertRefused("weatherNotSafe", r.start());
    r.gates.weather = Safety::Verdict::Safe;
    r.gates.daylightCloses = true;
    r.gates.daylightText = "Sun 3.0° ≥ 0.0°";
    assertRefused("daylight", r.start());
    r.gates.daylightCloses = false;
    r.gates.parked = false;
    r.gates.parkText = "N.I.N.A. not responding";
    assertRefused("mountNotParked", r.start());
    r.gates.parked = true;
    r.gates.fittedSwitches = 1;
    r.gates.switch1 = RoofIo::Contact::Open;
    assertRefused("mountNotParked", r.start());
}

void test_starts_are_refused_while_something_blocks_an_open(void)
{
    Rig r;
    r.busy = true;
    assertRefused("busy", r.start());
}

void test_starts_are_refused_for_a_bad_request(void)
{
    Rig r;
    assertRefused("invalid", r.start(Where::Closed, false, 99));
    assertRefused("invalid", r.start(Where::Closed, false, 20001));
    Request req;
    req.attended = true;
    req.travelMm = 1800;
    StartInputs in = r.startInputs();
    in.limitMs = MIN_LIMIT_MS - 1;
    assertRefused("invalid", r.run.start(r.roof, req, in));
    in.limitMs = MAX_LIMIT_MS + 1;
    assertRefused("invalid", r.run.start(r.roof, req, in));
    Roof::Settings s;
    s.topDutyPercent = 40;
    r.roof.applySettings(s);
    assertRefused("invalid", r.start(Where::Closed, true));
    TEST_ASSERT_TRUE(r.start(Where::Closed, false).accepted);
}

void test_the_start_must_agree_with_the_hall_reading(void)
{
    Rig r;
    assertRefused("hallDisagrees", r.start(Where::PartWay));
    Rig between(900.0f);
    assertRefused("hallDisagrees", between.start(Where::Closed));
    assertRefused("hallDisagrees", between.start(Where::Open));
    TEST_ASSERT_TRUE(between.start(Where::PartWay).accepted);
}

void test_a_start_needs_the_roofs_readiness(void)
{
    Rig r;
    r.plant.supplyVolts = 10.0f;
    r.steps(100);
    assertRefused("supplyLow", r.start());
}

// --- Stops and endings ---------------------------------------------------------------------

void test_a_stop_mid_leg_ends_the_run_with_no_proposal(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(10000);
    r.roof.command(Roof::Command::Stop, Roof::Origin::Person);
    r.run.stopped("Stopped by a person");
    TEST_ASSERT_FALSE(r.run.active());
    TEST_ASSERT_TRUE(r.rep().state == RunState::Stopped);
    TEST_ASSERT_FALSE(r.rep().proposed);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
    r.steps(5000);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Stopped);
    TEST_ASSERT_TRUE(sameSettings(r.settings, r.roof.settings()));
}

void test_a_stop_in_a_pause_ends_the_run_and_nothing_more_moves(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose, Phase::Settle));
    r.run.stopped("Stopped by a person");
    r.steps(10000);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Open);
    TEST_ASSERT_TRUE(r.rep().state == RunState::Stopped);
}

void test_a_leg_stopped_by_anything_else_ends_the_run(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(2000);
    r.roof.command(Roof::Command::Stop, Roof::Origin::Client);
    r.step();
    TEST_ASSERT_TRUE(r.rep().state == RunState::Stopped);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Stopped);
}

void test_unsafe_weather_while_opening_stops_the_roof(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(10000);
    r.gates.weather = Safety::Verdict::Unsafe;
    r.gates.weatherText = "unsafe: rain";
    r.step();
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Weather);
    TEST_ASSERT_TRUE(r.rep().state == RunState::Stopped);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

void test_unsafe_weather_while_closing_lets_the_close_finish(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose));
    r.steps(10000);
    r.gates.weather = Safety::Verdict::Unsafe;
    r.step();
    TEST_ASSERT_TRUE(r.run.active());
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closing);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Weather);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

void test_daylight_in_a_pause_ends_the_run(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    r.gates.daylightCloses = true;
    r.step();
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Weather);
    r.steps(5000);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
}

void test_the_park_lost_while_opening_stops_the_roof(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(3000);
    r.gates.parked = false;
    r.step();
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Park);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
}

void test_the_park_lost_in_a_pause_ends_the_run(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    r.gates.parked = false;
    r.step();
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Park);
}

void test_the_park_lost_while_closing_is_the_interlock_fault(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose));
    r.steps(3000);
    r.gates.parked = false;
    r.step();
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::External);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Fault);
    TEST_ASSERT_TRUE(r.rep().state == RunState::Failed);
}

void test_a_blocked_leg_waits_then_fails(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    r.busy = true;
    r.steps(SETTLE_MS + 2 * STEP_MS);
    TEST_ASSERT_TRUE(r.rep().phase == Phase::Wait);
    r.steps(LEG_WAIT_MS - 4 * STEP_MS);
    TEST_ASSERT_TRUE(r.run.active());
    r.steps(4 * STEP_MS);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Busy);
    TEST_ASSERT_TRUE(r.rep().state == RunState::Failed);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
}

void test_a_blocked_leg_goes_once_unblocked(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    r.busy = true;
    r.steps(5000);
    r.busy = false;
    r.steps(100);
    TEST_ASSERT_TRUE(r.rep().phase == Phase::Drive);
}

void test_the_automatic_close_takes_over(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose));
    r.run.yieldToClose(r.roof);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::CloseSequence);
    // The closing leg carries on as the close.
    TEST_ASSERT_TRUE(r.roof.check(Roof::Command::Close, Roof::Origin::Automatic).accepted);
    r.steps(200000);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
}

void test_wants_leg_only_when_a_leg_is_due(void)
{
    Rig r;
    TEST_ASSERT_FALSE(r.run.wantsLeg(r.nowMs));
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_FALSE(r.run.wantsLeg(r.nowMs));
    TEST_ASSERT_TRUE(r.run.wantsLeg(r.nowMs + SETTLE_MS));
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    TEST_ASSERT_FALSE(r.run.wantsLeg(r.nowMs + SETTLE_MS));
}

// --- Polarity and thresholds ---------------------------------------------------------------

void test_the_opposite_polarity_is_proposed(void)
{
    Rig r;
    r.plant.highEndClosed = true; // the setting says open reads high
    r.steps(200);
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
    TEST_ASSERT_TRUE(r.rep().proposal.hall);
    TEST_ASSERT_TRUE(r.rep().proposal.hallHighEnd == End::Closed);
    TEST_ASSERT_TRUE(r.plant.positionMm < 30.0f);
}

void test_same_pole_magnets_return_to_closed_and_withhold_the_thresholds(void)
{
    Rig r;
    r.plant.lowMagnetCounts = 3400; // both ends read high
    r.steps(200);
    TEST_ASSERT_TRUE(r.start(Where::Closed, true).accepted);
    TEST_ASSERT_TRUE(r.finish());
    const Report &rep = r.rep();
    TEST_ASSERT_TRUE(rep.state == RunState::Done);
    TEST_ASSERT_EQUAL_UINT8(2, rep.measured.legCount); // no top-duty pass
    TEST_ASSERT_FALSE(rep.proposal.hall);
    TEST_ASSERT_EQUAL_STRING("samePole", rep.proposal.hallProblem.reason.c_str());
    TEST_ASSERT_TRUE(rep.proposal.speed);
    TEST_ASSERT_TRUE(r.plant.positionMm < 30.0f);
}

void test_a_weak_magnet_withholds_the_thresholds(void)
{
    Roof::Settings s;
    s.hallHighCounts = 2200;
    Rig r(0.0f, s);
    r.plant.highMagnetCounts = 2250;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
    TEST_ASSERT_EQUAL_STRING("weakEnd", r.rep().proposal.hallProblem.reason.c_str());
}

void test_a_fault_cleared_before_the_next_step_still_ends_the_run(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    r.steps(400); // in the pause, before the first leg
    r.roof.raiseFault(Roof::Fault{FaultKind::External, 1, 0});
    r.roof.clearFault(Roof::Origin::Person);
    r.step();
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Fault);
    r.steps(5000);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
}

void test_the_automatic_close_stops_an_opening_leg(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(3000);
    r.run.yieldToClose(r.roof);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::CloseSequence);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_FALSE(r.roof.status().calibrating);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_run_from_closed_measures_and_proposes);
    RUN_TEST(test_a_run_from_part_way_closes_first);
    RUN_TEST(test_a_run_from_open_closes_first);
    RUN_TEST(test_the_top_pass_measures_two_more_legs_at_the_top_duty);
    RUN_TEST(test_later_legs_are_bounded_by_the_measured_ones);
    RUN_TEST(test_each_end_waits_a_second_at_rest);
    RUN_TEST(test_a_start_needs_the_attended_confirmation);
    RUN_TEST(test_a_second_start_is_refused_while_running);
    RUN_TEST(test_starts_are_refused_on_the_close_sequence_fault_and_motion);
    RUN_TEST(test_starts_are_refused_unless_the_weather_is_safe_and_the_mount_parked);
    RUN_TEST(test_starts_are_refused_while_something_blocks_an_open);
    RUN_TEST(test_starts_are_refused_for_a_bad_request);
    RUN_TEST(test_the_start_must_agree_with_the_hall_reading);
    RUN_TEST(test_a_start_needs_the_roofs_readiness);
    RUN_TEST(test_a_stop_mid_leg_ends_the_run_with_no_proposal);
    RUN_TEST(test_a_stop_in_a_pause_ends_the_run_and_nothing_more_moves);
    RUN_TEST(test_a_leg_stopped_by_anything_else_ends_the_run);
    RUN_TEST(test_unsafe_weather_while_opening_stops_the_roof);
    RUN_TEST(test_unsafe_weather_while_closing_lets_the_close_finish);
    RUN_TEST(test_daylight_in_a_pause_ends_the_run);
    RUN_TEST(test_the_park_lost_while_opening_stops_the_roof);
    RUN_TEST(test_the_park_lost_in_a_pause_ends_the_run);
    RUN_TEST(test_the_park_lost_while_closing_is_the_interlock_fault);
    RUN_TEST(test_a_blocked_leg_waits_then_fails);
    RUN_TEST(test_a_blocked_leg_goes_once_unblocked);
    RUN_TEST(test_the_automatic_close_takes_over);
    RUN_TEST(test_wants_leg_only_when_a_leg_is_due);
    RUN_TEST(test_a_fault_cleared_before_the_next_step_still_ends_the_run);
    RUN_TEST(test_the_automatic_close_stops_an_opening_leg);
    RUN_TEST(test_the_opposite_polarity_is_proposed);
    RUN_TEST(test_same_pole_magnets_return_to_closed_and_withhold_the_thresholds);
    RUN_TEST(test_a_weak_magnet_withholds_the_thresholds);
    return UNITY_END();
}
