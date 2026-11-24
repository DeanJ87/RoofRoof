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
    // Which limit to raise: the setting, and the slow duty for a slow leg.
    TEST_ASSERT_TRUE(r.rep().endingText.find("roof.calibrationLimitSeconds (180 s): raise it, up to 600 s") != std::string::npos);
    TEST_ASSERT_TRUE(r.rep().endingText.find("roof.slowDutyPercent (40 %)") != std::string::npos);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

// A slow roof whose slow travel doesn't fit the limit: the first leg's timeout
// names the setting; with the limit raised the run finishes, and a slow leg
// near the limit is warned about, naming the slow duty.
void test_a_slow_roof_names_the_limit_and_the_slow_duty(void)
{
    Rig r(1850.0f);
    r.plant.speedMmPerSecond = 10.0f; // 4 mm/s at 40 %: 462 s a slow travel
    TEST_ASSERT_TRUE(r.start(Where::Open).accepted);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::Timeout);
    TEST_ASSERT_TRUE(r.rep().endingText.find("Fault on the first close") != std::string::npos);
    TEST_ASSERT_TRUE(r.rep().endingText.find("roof.calibrationLimitSeconds (180 s)") != std::string::npos);

    Rig slow(1850.0f);
    slow.plant.speedMmPerSecond = 10.0f;
    slow.limitMs = MAX_LIMIT_MS;
    TEST_ASSERT_TRUE(slow.start(Where::Open).accepted);
    TEST_ASSERT_TRUE(slow.finish(3000000));
    TEST_ASSERT_TRUE(slow.rep().state == RunState::Done);
    bool warned = false;
    for (const Problem &w : slow.rep().proposal.warnings)
        warned = warned || (w.reason == "slowLegsLong" && w.text.find("roof.slowDutyPercent (40 %)") != std::string::npos &&
                            w.text.find("of the 600 s calibration limit") != std::string::npos);
    TEST_ASSERT_TRUE(warned);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 4.0f, slow.rep().proposal.slowSpeedMmPerSecond);
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
    RUN_TEST(test_a_slow_roof_names_the_limit_and_the_slow_duty);
    RUN_TEST(test_a_jam_with_current_sense_is_over_current);
    RUN_TEST(test_a_motor_wired_backwards_doesnt_leave);
    RUN_TEST(test_a_low_supply_mid_leg_faults);
    RUN_TEST(test_a_hall_rail_mid_leg_faults);
    return UNITY_END();
}
