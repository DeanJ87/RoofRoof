// Bench mode's pure decisions (spec 011 FR-021, FR-023): the supply guard that
// stops bench firmware simulating a roof on a powered board, and the pin rule
// together with the plant (the plant only moves on what the logic decided,
// while the pins stay low).

#include <cmath>
#include <unity.h>

#include "BenchGuard.h"
#include "DriverLogic.h"
#include "RoofPlant.h"

using namespace SQM;
using SQM::RoofPlant::BenchSupplyGuard;

void setUp(void) {}
void tearDown(void) {}

void test_bare_board_never_trips(void)
{
    BenchSupplyGuard guard;
    for (uint32_t t = 0; t < 600000; t += 20)
        TEST_ASSERT_FALSE(guard.update(0.4f, t));
    TEST_ASSERT_FALSE(guard.update(5.99f, 600020));
    TEST_ASSERT_FALSE(guard.tripped());
}

// The limit is >=: 6.0 V counts. One second without a break trips it.
void test_supply_at_the_limit_for_a_second_trips(void)
{
    BenchSupplyGuard guard;
    TEST_ASSERT_FALSE(guard.update(6.0f, 1000));
    TEST_ASSERT_FALSE(guard.update(6.0f, 1999));
    TEST_ASSERT_TRUE(guard.update(6.0f, 2000));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, guard.measured());
}

void test_a_break_restarts_the_second(void)
{
    BenchSupplyGuard guard;
    guard.update(12.4f, 0);
    guard.update(12.4f, 900);
    TEST_ASSERT_FALSE(guard.update(1.0f, 920)); // a clean reading breaks the run
    TEST_ASSERT_FALSE(guard.update(12.4f, 940));
    TEST_ASSERT_FALSE(guard.update(12.4f, 1900));
    TEST_ASSERT_TRUE(guard.update(12.4f, 1940));
}

// Doubt is a powered roof: no reading counts like 12 V.
void test_unreadable_counts_as_powered(void)
{
    BenchSupplyGuard guard;
    guard.update(NAN, 0);
    TEST_ASSERT_TRUE(guard.update(INFINITY, 1000));
    TEST_ASSERT_TRUE(std::isinf(guard.measured()));
    TEST_ASSERT_TRUE(RoofPlant::supplyPresent(NAN));
    TEST_ASSERT_FALSE(RoofPlant::supplyPresent(0.0f));
}

// Once tripped, it stays tripped for the boot, whatever it reads later.
void test_trip_is_latched(void)
{
    BenchSupplyGuard guard;
    guard.update(12.4f, 0);
    TEST_ASSERT_TRUE(guard.update(12.4f, 1000));
    for (uint32_t t = 1020; t < 100000; t += 20)
        TEST_ASSERT_TRUE(guard.update(0.0f, t));
    TEST_ASSERT_EQUAL_FLOAT(12.4f, guard.measured());
}

void test_confirm_survives_a_millis_wrap(void)
{
    BenchSupplyGuard guard;
    guard.update(12.0f, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(guard.update(12.0f, 0x00000100u)); // 512 ms later
    TEST_ASSERT_TRUE(guard.update(12.0f, 0x00000300u));  // 1024 ms later
}

// In a bench build the plant follows the logical levels while the pins stay low.
void test_plant_moves_on_logic_while_pins_stay_low(void)
{
    RoofPlant::Plant plant = RoofPlant::forSettings(Roof::Settings(), 900.0f);
    const Driver::Outputs logical = Driver::outputsFor(Driver::Command::Forward, 255);
    const Driver::Outputs pins = Driver::physicalOutputs(logical, true);
    TEST_ASSERT_FALSE(pins.enable);
    TEST_ASSERT_TRUE(plant.drive(RoofPlant::outputOf(logical), 1000) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plant.drive(RoofPlant::outputOf(pins), 1000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bare_board_never_trips);
    RUN_TEST(test_supply_at_the_limit_for_a_second_trips);
    RUN_TEST(test_a_break_restarts_the_second);
    RUN_TEST(test_unreadable_counts_as_powered);
    RUN_TEST(test_trip_is_latched);
    RUN_TEST(test_confirm_survives_a_millis_wrap);
    RUN_TEST(test_plant_moves_on_logic_while_pins_stay_low);
    return UNITY_END();
}
