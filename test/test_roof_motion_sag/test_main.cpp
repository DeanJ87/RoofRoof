// Spec 040 (F34, amends spec 036 FR-014): the movement check counts any supply
// reading that sags by roof.motionSagVolts after the start-up surge, not a sag
// held CONFIRM_MS. At full speed the PWM-loaded supply reading is noisy and swings
// across the threshold, so a held sag never confirmed and a normal open latched
// NoMovement part-way. A drive that doesn't sag the supply at all (F21) still does.

#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "Config.h" // for the library finder: RoofLogic builds with ConfigModel
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Settings watched()
    {
        Settings s;
        s.motionSagVolts = 0.2f;
        s.slowDutyPercent = 40;
        return s;
    }
} // namespace

namespace
{
    // Drives a move from mid-travel (the Hall flat between the magnets) with
    // the supply reading `supply(drivenMs)` instead of the model's, until a
    // fault or `maxDrivenMs` of driving. Returns the driven time.
    template <typename Supply> uint32_t driveWithSupply(Bench &b, Supply supply, uint32_t maxDrivenMs)
    {
        uint32_t drivenMs = 0;
        while (b.roof.status().fault.kind == FaultKind::None && drivenMs < maxDrivenMs)
        {
            b.sim.drive(b.roof.output(), b.stepMs);
            b.nowMs += b.stepMs;
            Readings r = b.sim.readings(b.nowMs);
            if (b.roof.status().driving)
            {
                r.supplyVolts = supply(drivenMs);
                drivenMs += b.stepMs;
            }
            b.roof.tick(r);
        }
        return drivenMs;
    }

    const float REST_VOLTS = 12.6f; // the model's supply with the driver off
} // namespace

// The field open (F34): at full speed the supply reading swings between 0.1 V
// and 0.6 V below rest from one reading to the next, never 0.2 V for 250 ms.
// The roof is moving: no NoMovement fault in 30 s of driving.
void test_a_noisy_sag_is_movement(void)
{
    Bench b(watched(), 900.0f);
    b.sim.sagVolts = 0.0f;
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    const uint32_t drivenMs = driveWithSupply(b, [](uint32_t ms) { return REST_VOLTS - ((ms / 20) % 2 == 0 ? 0.1f : 0.6f); }, 30000);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind != FaultKind::NoMovement);
    TEST_ASSERT_UINT32_WITHIN(20, 30000, drivenMs);
}

// Mostly under the threshold, with a short dip past it every few seconds (the
// 1 Hz trace: 0.15-0.3 V, a 0.63 V deepest): still movement.
void test_an_occasional_deep_sag_is_movement(void)
{
    Bench b(watched(), 900.0f);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    driveWithSupply(b, [](uint32_t ms) { return REST_VOLTS - (ms % 3000 < 20 ? 0.63f : 0.15f); }, 30000);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind != FaultKind::NoMovement);
}

// F21 still faults: no sag at all and the Hall flat stops it after the surge
// and the check time, the fault giving the deepest sag (0) and the setting.
void test_a_flat_supply_is_still_no_movement(void)
{
    Bench b(watched(), 900.0f);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    const uint32_t drivenMs = driveWithSupply(b, [](uint32_t) { return REST_VOLTS; }, 60000);
    const Status st = b.roof.status();
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::NoMovement);
    TEST_ASSERT_UINT32_WITHIN(60, BLIND_SURGE_MS + 10000, drivenMs);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, st.fault.limit);
}

// The start-up surge doesn't count: a deep sag only in the first second, then
// a flat supply, is no movement.
void test_the_start_up_surge_is_not_movement(void)
{
    Bench b(watched(), 900.0f);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    const uint32_t drivenMs = driveWithSupply(b, [](uint32_t ms) { return REST_VOLTS - (ms < BLIND_SURGE_MS - 40 ? 1.5f : 0.05f); }, 60000);
    const Status st = b.roof.status();
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::NoMovement);
    TEST_ASSERT_UINT32_WITHIN(60, BLIND_SURGE_MS + 10000, drivenMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.05f, st.fault.measured);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_noisy_sag_is_movement);
    RUN_TEST(test_an_occasional_deep_sag_is_movement);
    RUN_TEST(test_a_flat_supply_is_still_no_movement);
    RUN_TEST(test_the_start_up_surge_is_not_movement);
    return UNITY_END();
}
