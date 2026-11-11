// Spec 016: a calibration run on a faulty simulated roof: each plant fault
// gives the existing motion fault on the leg, within its limit, and a failed
// run with no proposal.

#include <unity.h>

#include "../calibration_rig/CalibrationRig.h"

using namespace CalibTest;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

// --- The plant's faults --------------------------------------------------------------------

void test_a_missing_magnet_times_out_at_the_limit(void)
{
    Rig r;
    r.plant.openMagnetMissing = true;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::Timeout);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(LIMIT_MS), r.roof.status().fault.limit);
    TEST_ASSERT_TRUE(r.rep().state == RunState::Failed);
    TEST_ASSERT_TRUE(r.rep().endingText.find("slow open") != std::string::npos);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

void test_a_jam_with_current_sense_is_over_current(void)
{
    Roof::Settings s;
    s.currentSenseEnabled = true;
    Rig r(0.0f, s);
    r.plant.stallAmps = 18.0f;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(5000);
    r.plant.jammed = true;
    TEST_ASSERT_TRUE(r.finish(2000));
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::OverCurrent);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Fault);
}

void test_a_motor_wired_backwards_doesnt_leave(void)
{
    Rig r;
    r.plant.reversedWiring = true;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.finish(30000));
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::DidNotLeave);
}

void test_a_low_supply_mid_leg_faults(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(3000);
    r.plant.supplyVolts = 10.0f;
    TEST_ASSERT_TRUE(r.finish(2000));
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::SupplyLow);
}

void test_a_hall_rail_mid_leg_faults(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.plant.railCounts = 4095;
    TEST_ASSERT_TRUE(r.finish(2000));
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::HallRail);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_missing_magnet_times_out_at_the_limit);
    RUN_TEST(test_a_jam_with_current_sense_is_over_current);
    RUN_TEST(test_a_motor_wired_backwards_doesnt_leave);
    RUN_TEST(test_a_low_supply_mid_leg_faults);
    RUN_TEST(test_a_hall_rail_mid_leg_faults);
    return UNITY_END();
}
