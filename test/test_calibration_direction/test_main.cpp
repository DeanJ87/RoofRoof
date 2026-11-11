// Spec 016: the run's checks on the motor's direction and speed profile: the
// person's confirmation of the open end after the slow open (a motor running
// the other way, a wrong answer), and the top-duty legs reaching their magnet
// at the slow duty on a motor whose speed is affine in the duty.

#include <unity.h>

#include "../calibration_rig/CalibrationRig.h"
#include "MotionProfile.h"

using namespace CalibTest;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

void test_top_legs_arrive_at_the_slow_duty_on_an_affine_motor(void)
{
    Rig r;
    r.plant.deadbandFraction = 0.2f;  // slow at 40 % is a quarter of full speed, not 40 %
    r.plant.speedMmPerSecond = 60.0f; // so a slow travel still fits the limit
    TEST_ASSERT_TRUE(r.start(Where::Closed, true).accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::TopOpen));
    uint8_t lastDuty = 0;
    while (r.run.active() && r.rep().leg == LegKind::TopOpen && r.rep().phase == Phase::Drive)
    {
        if (r.roof.output().duty > 0)
            lastDuty = r.roof.output().duty;
        r.step();
    }
    TEST_ASSERT_EQUAL_UINT8(Roof::toDuty(0.4f), lastDuty);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
}

void test_the_slow_close_waits_for_the_open_end_to_be_confirmed(void)
{
    Rig r;
    r.answerTruthfully = false;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose, Phase::Confirm));
    TEST_ASSERT_FALSE(r.run.wantsLeg(r.nowMs));
    r.steps(60000);
    TEST_ASSERT_TRUE(r.rep().phase == Phase::Confirm);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Open);
    TEST_ASSERT_TRUE(r.run.confirmOpen(true).accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose));
    TEST_ASSERT_FALSE(r.run.confirmOpen(true).accepted); // not waiting any more
}

void test_no_confirmation_ends_the_run(void)
{
    Rig r;
    r.answerTruthfully = false;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowClose, Phase::Confirm));
    r.steps(CONFIRM_WAIT_MS + 1000);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Unconfirmed);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

void test_a_backwards_motor_from_part_way_is_caught_at_the_confirmation(void)
{
    // The first close drives to the open end (backwards); the slow open then
    // reaches the closed end, which looks consistent to the run. The person sees it.
    Rig r(900.0f);
    r.plant.reversedWiring = true;
    TEST_ASSERT_TRUE(r.start(Where::PartWay).accepted);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().ending == Ending::WrongEnd);
    TEST_ASSERT_TRUE(r.rep().state == RunState::Failed);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

void test_a_wrong_answer_and_a_backwards_motor_are_caught_too(void)
{
    // The roof is open but the person says closed, and the motor is backwards:
    // the slow open drives it to the closed end.
    Rig r(1850.0f);
    r.plant.reversedWiring = true;
    r.plant.highEndClosed = false;
    TEST_ASSERT_TRUE(r.start(Where::Closed).accepted);
    TEST_ASSERT_TRUE(r.finish());
    TEST_ASSERT_TRUE(r.rep().ending == Ending::WrongEnd);
    TEST_ASSERT_FALSE(r.rep().proposed);
}

void test_top_legs_arrive_slow_with_a_large_deadband(void)
{
    struct Case
    {
        uint8_t slow;
        float deadband;
    };
    for (const Case c : {Case{40, 0.30f}, Case{30, 0.20f}, Case{40, 0.20f}})
    {
        Roof::Settings s;
        s.slowDutyPercent = c.slow;
        Rig r(0.0f, s);
        r.plant.deadbandFraction = c.deadband;
        r.plant.speedMmPerSecond = 120.0f; // a slow travel still fits the limit
        TEST_ASSERT_TRUE(r.start(Where::Closed, true).accepted);
        TEST_ASSERT_TRUE(r.until(LegKind::TopOpen));
        uint8_t lastDuty = 0;
        while (r.run.active() && r.rep().leg == LegKind::TopOpen && r.rep().phase == Phase::Drive)
        {
            if (r.roof.output().duty > 0)
                lastDuty = r.roof.output().duty;
            r.step();
        }
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(
            Roof::toDuty(c.slow / 100.0f), lastDuty, "the top-duty open reached its magnet above the slow duty");
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_top_legs_arrive_at_the_slow_duty_on_an_affine_motor);
    RUN_TEST(test_the_slow_close_waits_for_the_open_end_to_be_confirmed);
    RUN_TEST(test_no_confirmation_ends_the_run);
    RUN_TEST(test_a_backwards_motor_from_part_way_is_caught_at_the_confirmation);
    RUN_TEST(test_a_wrong_answer_and_a_backwards_motor_are_caught_too);
    RUN_TEST(test_top_legs_arrive_slow_with_a_large_deadband);
    return UNITY_END();
}
