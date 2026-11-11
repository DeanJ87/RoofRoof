// Every motion fault stops the motor and stays stopped (spec 002 US2, FR-016 to
// FR-019; constitution I): each fault kind at its boundary, the latch, who may
// clear it, and a fault restored at start-up.

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
    bool isOff(const Output &out)
    {
        return out.command == Driver::Command::Off && out.duty == 0;
    }

    // Steps until a fault latches. Checks that the step before it was still
    // driving and that the driver is off from the faulting step on.
    Status runToFault(Bench &b, uint32_t maxMs, Status *before = nullptr)
    {
        Status previous = b.roof.status();
        for (uint32_t t = 0; t < maxMs; t += b.stepMs)
        {
            const Output out = b.step();
            if (b.roof.status().fault.kind != FaultKind::None)
            {
                TEST_ASSERT_TRUE(isOff(out));
                TEST_ASSERT_TRUE(isOff(b.roof.output()));
                TEST_ASSERT_TRUE(b.state() == State::Error);
                TEST_ASSERT_FALSE(b.roof.status().positionKnown);
                if (before)
                    *before = previous;
                return b.roof.status();
            }
            previous = b.roof.status();
        }
        TEST_FAIL_MESSAGE("no fault latched");
        return b.roof.status();
    }

    // Opens a closed bench roof for `ms` and stops, leaving it part-way and known.
    void stopPartWay(Bench &b, uint32_t ms)
    {
        TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
        b.steps(static_cast<int>(ms / b.stepMs));
        b.command(Command::Stop);
        b.steps(30); // past the dead time
        TEST_ASSERT_TRUE(b.state() == State::Stopped);
    }

    Settings withCurrentSense()
    {
        Settings s;
        s.currentSenseEnabled = true;
        return s;
    }
} // namespace

// --- Timeouts (FR-014, FR-015) ------------------------------------------------

// The open magnet is missing: the move runs into the end stop and times out at
// exactly its limit, still driving one step before.
void test_fault_timeout_at_the_limit_not_before(void)
{
    Bench b;
    b.sim.openMagnetMissing = true;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    Status before;
    const Status fault = runToFault(b, 200000, &before);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::Timeout);
    const uint32_t limit = moveTimeoutMs(Settings(), 1850.0f);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(limit), fault.fault.limit);
    TEST_ASSERT_TRUE(fault.fault.measured >= static_cast<float>(limit));
    TEST_ASSERT_TRUE(fault.fault.measured < static_cast<float>(limit + 20));
    TEST_ASSERT_TRUE(before.driving);
    TEST_ASSERT_TRUE(before.moveElapsedMs < limit);
}

void test_fault_timeout_of_a_move_from_unknown(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.closedMagnetMissing = true;
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    const Status fault = runToFault(b, 300000);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::Timeout);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(unknownMoveTimeoutMs(Settings())), fault.fault.limit);
}

// A jam at the start magnet (or a motor pushing the wrong way into the end
// stop) is caught at the leave time, not the full timeout.
void test_fault_did_not_leave_the_start_magnet(void)
{
    Bench b;
    b.sim.jammed = true;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    Status before;
    const Status fault = runToFault(b, 20000, &before);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_EQUAL_FLOAT(10000.0f, fault.fault.limit);
    TEST_ASSERT_TRUE(fault.fault.measured >= 10000.0f && fault.fault.measured < 10020.0f);
    TEST_ASSERT_TRUE(before.driving && before.moveElapsedMs < 10000);
}

void test_fault_did_not_leave_with_the_motor_wired_backwards(void)
{
    Bench b;
    b.sim.reversedWiring = true;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    const Status fault = runToFault(b, 20000);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.sim.positionMm);
}

// --- Magnets ------------------------------------------------------------------

void test_fault_wrong_magnet_the_other_ends(void)
{
    Bench b;
    stopPartWay(b, 8000);
    b.sim.reversedWiring = true; // "open" now drives toward closed
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    const Status fault = runToFault(b, 200000);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Closed), fault.fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Open), fault.fault.limit);
}

void test_fault_wrong_magnet_the_start_magnet_again(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(5000 / 20);
    TEST_ASSERT_TRUE(b.state() == State::Opening);
    b.sim.reversedWiring = true; // the roof starts coming back
    const Status fault = runToFault(b, 60000);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Closed), fault.fault.measured);
}

// --- Hall rails (FR-019) --------------------------------------------------------

void test_fault_hall_rail_at_rest(void)
{
    for (int rail : {0, 30, 4065, 4095})
    {
        Bench b;
        b.sim.railCounts = rail;
        b.steps(2);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None); // not yet confirmed
        b.step();
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
        TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(rail), b.roof.status().fault.measured);
        TEST_ASSERT_EQUAL_FLOAT(rail < 2000 ? 30.0f : 4065.0f, b.roof.status().fault.limit);
        TEST_ASSERT_TRUE(b.state() == State::Error);
    }
}

void test_hall_just_inside_the_rails_is_no_fault(void)
{
    for (int counts : {31, 4064})
    {
        Bench b;
        b.sim.railCounts = counts;
        b.steps(20);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    }
}

void test_fault_hall_rail_while_moving(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(200);
    b.sim.railCounts = 4095; // the sensor's ground wire falls off
    const Status fault = runToFault(b, 1000);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::HallRail);
}

void test_a_rail_relatches_after_clear_while_it_lasts(void)
{
    Bench b;
    b.sim.railCounts = 0;
    b.steps(3);
    TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
    b.step();
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    b.sim.railCounts = -1;
    b.steps(3);
    TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
    b.steps(3);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

// --- Current (FR-016) -----------------------------------------------------------

void test_fault_over_current_after_the_confirm_time(void)
{
    Bench b(withCurrentSense());
    b.sim.runningAmps = 10.0f; // at the limit counts
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.step(); // driving starts; the sim reports current from the next step
    Status before;
    const Status fault = runToFault(b, 1000, &before);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::OverCurrent);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, fault.fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, fault.fault.limit);
    // Current is first seen 20 ms into the drive. The step before the fault had
    // seen it for less than CONFIRM_MS and was still driving; the fault step
    // (20 ms later) had seen it for at least CONFIRM_MS.
    const uint32_t firstSeenMs = 20;
    TEST_ASSERT_TRUE(before.driving);
    TEST_ASSERT_TRUE(before.moveElapsedMs - firstSeenMs < CONFIRM_MS);
    TEST_ASSERT_TRUE(before.moveElapsedMs + 20 - firstSeenMs >= CONFIRM_MS);
}

void test_current_just_under_the_limit_is_no_fault(void)
{
    Bench b(withCurrentSense());
    b.sim.runningAmps = 9.99f;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_a_current_spike_shorter_than_the_confirm_time_is_ignored(void)
{
    Bench b(withCurrentSense());
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(100);
    b.sim.runningAmps = 25.0f;
    b.steps(12); // 240 ms
    b.sim.runningAmps = 3.0f;
    b.steps(2);
    b.sim.runningAmps = 25.0f; // the count restarts after a good reading
    b.steps(12);
    b.sim.runningAmps = 3.0f;
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_over_current_ignored_when_current_sense_is_off(void)
{
    Bench b;
    b.sim.runningAmps = 30.0f;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_fault_current_unreadable_during_a_move(void)
{
    Bench b(withCurrentSense());
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(100);
    b.sim.currentFitted = false;
    b.steps(12);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    const Status fault = runToFault(b, 100);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::CurrentUnreadable);
    TEST_ASSERT_TRUE(std::isnan(fault.fault.measured));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, fault.fault.limit);
}

// --- Supply (FR-016) ------------------------------------------------------------

void test_fault_supply_low_during_a_move(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(100);
    b.sim.supplyVolts = 10.5f;
    b.steps(12); // 240 ms: not yet
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    const Status fault = runToFault(b, 100);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::SupplyLow);
    TEST_ASSERT_EQUAL_FLOAT(10.5f, fault.fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, fault.fault.limit);
}

void test_fault_supply_unreadable_during_a_move(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(100);
    b.sim.supplyVolts = NAN;
    const Status fault = runToFault(b, 400);
    TEST_ASSERT_TRUE(fault.fault.kind == FaultKind::SupplyLow);
    TEST_ASSERT_TRUE(std::isnan(fault.fault.measured));
}

void test_a_supply_dip_shorter_than_the_confirm_time_is_ignored(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.step();
    b.sim.supplyVolts = 9.0f; // the start-up sag
    b.steps(12);
    b.sim.supplyVolts = 11.0f; // at the minimum is fine
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

// --- Late steps (constitution I: a missed control-loop deadline) ------------------

void test_fault_late_step_while_driving(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(50);
    b.stepMs = MAX_STEP_GAP_MS; // exactly the allowed gap
    b.step();
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    b.stepMs = MAX_STEP_GAP_MS + 1;
    const Output out = b.step();
    TEST_ASSERT_TRUE(isOff(out));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::LateStep);
    TEST_ASSERT_EQUAL_FLOAT(101.0f, b.roof.status().fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, b.roof.status().fault.limit);
}

void test_a_late_step_at_rest_is_no_fault(void)
{
    Bench b;
    b.stepMs = 5000;
    b.steps(3);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

// --- The latch (FR-017, FR-018) ----------------------------------------------------

void test_raise_fault_stops_at_once(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(200);
    TEST_ASSERT_FALSE(isOff(b.roof.output()));
    b.roof.raiseFault(Fault{FaultKind::External, 1.0f, 2.0f}, b.nowMs);
    TEST_ASSERT_TRUE(isOff(b.roof.output()));
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::External);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, b.roof.status().fault.measured);
}

void test_a_fault_kind_none_still_latches(void)
{
    Bench b;
    b.roof.raiseFault(Fault{}, b.nowMs);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::External);
    TEST_ASSERT_TRUE(b.state() == State::Error);
}

void test_a_fault_refuses_every_move_until_a_person_clears_it(void)
{
    Bench b;
    b.roof.raiseFault(Fault{FaultKind::External, 0.0f, 0.0f}, b.nowMs);
    for (Origin origin : {Origin::Person, Origin::Automatic})
        for (Command c : {Command::Open, Command::Close})
        {
            const CommandResult r = b.command(c, origin);
            TEST_ASSERT_FALSE(r.accepted);
            TEST_ASSERT_TRUE(r.reason == Refusal::FaultLatched);
        }
    TEST_ASSERT_TRUE(b.command(Command::Stop).accepted);
    TEST_ASSERT_TRUE(b.command(Command::Abort).accepted);
    for (int i = 0; i < 1000; ++i)
        TEST_ASSERT_TRUE(isOff(b.step()));
    const CommandResult automatic = b.roof.clearFault(Origin::Automatic);
    TEST_ASSERT_FALSE(automatic.accepted);
    TEST_ASSERT_TRUE(automatic.reason == Refusal::NotAPerson);
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Closed); // the magnet is read
    TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Automatic).accepted);
}

void test_clearing_between_the_magnets_leaves_the_position_unknown(void)
{
    Bench b;
    b.sim.openMagnetMissing = true;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(1000);
    b.sim.supplyVolts = 5.0f;
    runToFault(b, 1000);
    b.sim.supplyVolts = 12.6f;
    b.step();
    TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).reason == Refusal::PositionUnknown);
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Person).accepted);
    TEST_ASSERT_TRUE(b.roof.status().fromUnknown);
}

// A restart never clears a fault: spec 003 restores the saved one before the first step.
void test_a_restored_fault_latches_before_the_first_step(void)
{
    Controller roof;
    roof.raiseFault(Fault{FaultKind::Timeout, 70000.0f, 65000.0f}, 0);
    Readings readings;
    readings.hallCounts = 700; // at the closed magnet
    readings.supplyVolts = 12.6f;
    for (uint32_t i = 1; i <= 10; ++i)
    {
        readings.nowMs = i * 20;
        TEST_ASSERT_TRUE(isOff(roof.tick(readings)));
    }
    TEST_ASSERT_TRUE(roof.status().state == State::Error);
    TEST_ASSERT_TRUE(roof.status().fault.kind == FaultKind::Timeout);
    TEST_ASSERT_TRUE(roof.command(Command::Open, Origin::Person, 220).reason == Refusal::FaultLatched);
    TEST_ASSERT_TRUE(roof.clearFault(Origin::Person).accepted);
    TEST_ASSERT_TRUE(roof.status().state == State::Closed);
}

void test_the_first_fault_is_the_one_kept(void)
{
    Bench b;
    b.roof.raiseFault(Fault{FaultKind::Timeout, 1.0f, 2.0f}, b.nowMs);
    b.sim.railCounts = 0;
    b.steps(5);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::Timeout);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fault_timeout_at_the_limit_not_before);
    RUN_TEST(test_fault_timeout_of_a_move_from_unknown);
    RUN_TEST(test_fault_did_not_leave_the_start_magnet);
    RUN_TEST(test_fault_did_not_leave_with_the_motor_wired_backwards);
    RUN_TEST(test_fault_wrong_magnet_the_other_ends);
    RUN_TEST(test_fault_wrong_magnet_the_start_magnet_again);
    RUN_TEST(test_fault_hall_rail_at_rest);
    RUN_TEST(test_hall_just_inside_the_rails_is_no_fault);
    RUN_TEST(test_fault_hall_rail_while_moving);
    RUN_TEST(test_a_rail_relatches_after_clear_while_it_lasts);
    RUN_TEST(test_fault_over_current_after_the_confirm_time);
    RUN_TEST(test_current_just_under_the_limit_is_no_fault);
    RUN_TEST(test_a_current_spike_shorter_than_the_confirm_time_is_ignored);
    RUN_TEST(test_over_current_ignored_when_current_sense_is_off);
    RUN_TEST(test_fault_current_unreadable_during_a_move);
    RUN_TEST(test_fault_supply_low_during_a_move);
    RUN_TEST(test_fault_supply_unreadable_during_a_move);
    RUN_TEST(test_a_supply_dip_shorter_than_the_confirm_time_is_ignored);
    RUN_TEST(test_fault_late_step_while_driving);
    RUN_TEST(test_a_late_step_at_rest_is_no_fault);
    RUN_TEST(test_raise_fault_stops_at_once);
    RUN_TEST(test_a_fault_kind_none_still_latches);
    RUN_TEST(test_a_fault_refuses_every_move_until_a_person_clears_it);
    RUN_TEST(test_clearing_between_the_magnets_leaves_the_position_unknown);
    RUN_TEST(test_a_restored_fault_latches_before_the_first_step);
    RUN_TEST(test_the_first_fault_is_the_one_kept);
    return UNITY_END();
}
