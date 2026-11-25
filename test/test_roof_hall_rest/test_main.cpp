// Spec 027 (constitution 7.0.0, I): a Hall rail fault latched at rest clears
// itself after a minute of good readings at rest; one latched while driving
// waits for a person; a rail at rest just after start-up isn't judged.

#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // Steps until a fault latches (the driver is off from then on).
    Status runToFault(Bench &b, uint32_t maxMs)
    {
        for (uint32_t t = 0; t < maxMs; t += b.stepMs)
        {
            b.step();
            if (b.roof.status().fault.kind != FaultKind::None)
                return b.roof.status();
        }
        TEST_FAIL_MESSAGE("no fault latched");
        return b.roof.status();
    }
} // namespace

// Spec 027 (constitution 7.0.0, I): a rail latched at rest clears itself after
// HALL_REST_CLEAR_MS of good readings at rest; the position is read afresh.
void test_hall_rail_at_rest_clears_after_a_minute_of_good_readings(void)
{
    Bench b;
    b.sim.railCounts = 0;
    b.steps(3);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_TRUE(b.roof.faultClearsAtRest());
    b.sim.railCounts = -1;
    // The window empties and the level settles, then the minute runs.
    while (b.roof.status().hall == HallLevel::Rail || b.roof.status().hall == HallLevel::Unsettled)
        b.step();
    uint32_t goodMs = 0;
    while (b.roof.status().fault.kind == FaultKind::HallRail && goodMs < 2 * HALL_REST_CLEAR_MS)
    {
        b.step();
        goodMs += b.stepMs;
    }
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_UINT32_WITHIN(b.stepMs, HALL_REST_CLEAR_MS + b.stepMs, goodMs);
    TEST_ASSERT_TRUE(b.state() == State::Closed); // at the closed magnet again
    TEST_ASSERT_FALSE(b.roof.faultClearsAtRest());
}

// Spec 027: a rail at rest in the first moments after start-up (a power-up
// transient) is not judged; one after them is.
void test_a_rail_at_boot_is_not_judged(void)
{
    Settings settings;
    Controller roof(settings);
    RoofTest::RoofSim sim;
    sim.travelMm = static_cast<float>(settings.travelMm);
    sim.railCounts = 189;
    uint32_t now = 1000;
    for (uint32_t t = 0; t + 20 < HALL_BOOT_SETTLE_MS; t += 20, now += 20)
        roof.tick(sim.readings(now));
    TEST_ASSERT_TRUE(roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_FALSE(roof.check(Command::Open, Origin::Person).accepted); // nothing moves meanwhile
    for (int i = 0; i < 5; ++i, now += 20)
        roof.tick(sim.readings(now));
    TEST_ASSERT_TRUE(roof.status().fault.kind == FaultKind::HallRail);
}

// A rail reading in the minute starts it again.
void test_hall_rail_at_rest_restarts_its_minute_on_a_new_rail(void)
{
    Bench b;
    b.sim.railCounts = 0;
    b.steps(3);
    b.sim.railCounts = -1;
    b.steps(static_cast<int>(HALL_REST_CLEAR_MS / b.stepMs) - 100);
    b.sim.railCounts = 4095;
    b.steps(3);
    b.sim.railCounts = -1;
    b.steps(200);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    b.steps(static_cast<int>(HALL_REST_CLEAR_MS / b.stepMs));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

// A rail latched while driving waits for a person, however long the readings are good.
void test_hall_rail_while_moving_waits_for_a_person(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(200);
    b.sim.railCounts = 4095;
    runToFault(b, 1000);
    TEST_ASSERT_FALSE(b.roof.faultClearsAtRest());
    b.sim.railCounts = -1;
    b.steps(static_cast<int>(3 * HALL_REST_CLEAR_MS / b.stepMs));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_FALSE(b.roof.clearFault(Origin::Client).accepted);
    TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_hall_rail_at_rest_clears_after_a_minute_of_good_readings);
    RUN_TEST(test_hall_rail_at_rest_restarts_its_minute_on_a_new_rail);
    RUN_TEST(test_a_rail_at_boot_is_not_judged);
    RUN_TEST(test_hall_rail_while_moving_waits_for_a_person);
    return UNITY_END();
}
