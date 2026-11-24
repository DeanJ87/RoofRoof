// Spec 016: the legs from each start position, and the run's checks on the
// motor's direction and speed profile: the
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

// Each start position takes its own legs: from the closed end straight to the
// slow open; from the open end or part-way a close first. The run always ends
// closed, and the slow open always starts from the closed end.
void test_each_start_position_takes_its_legs(void)
{
    struct Case
    {
        Where at;
        float startMm;
        bool top;
        uint8_t total;
    };
    for (const Case c :
         {Case{Where::Closed, 0.0f, false, 2},
          Case{Where::Closed, 0.0f, true, 4},
          Case{Where::Open, 1850.0f, false, 3},
          Case{Where::Open, 1850.0f, true, 5},
          Case{Where::PartWay, 900.0f, false, 3},
          Case{Where::PartWay, 900.0f, true, 5}})
    {
        Rig r(c.startMm);
        TEST_ASSERT_TRUE(r.start(c.at, c.top).accepted);
        TEST_ASSERT_EQUAL_UINT8(c.total, r.rep().legTotal);
        TEST_ASSERT_TRUE(r.rep().leg == (c.at == Where::Closed ? LegKind::SlowOpen : LegKind::ToClosed));
        TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
        TEST_ASSERT_TRUE(r.plant.positionMm < 30.0f); // from the closed end
        TEST_ASSERT_TRUE(r.finish());
        TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
        TEST_ASSERT_EQUAL_UINT8(c.top ? 4 : 2, r.rep().measured.legCount);
        TEST_ASSERT_TRUE(r.rep().measured.legs[0].kind == LegKind::SlowOpen);
        TEST_ASSERT_TRUE(r.rep().measured.legs[1].kind == LegKind::SlowClose);
        TEST_ASSERT_TRUE(r.roof.status().state == State::Closed);
    }
}

// The reported failure: the roof at its open end but the start says closed. The
// run believes the person (the polarity may be the thing that's wrong), so its
// slow open can't leave the open end's magnet: a fault within the leave time,
// and the ending says what to check. The roof never moved off its end.
void test_a_start_that_says_closed_at_the_open_end_fails_safely_and_says_why(void)
{
    Rig r(1850.0f);
    TEST_ASSERT_TRUE(r.roof.status().state == State::Open);
    TEST_ASSERT_TRUE(r.start(Where::Closed).accepted);
    TEST_ASSERT_TRUE(r.finish(SETTLE_MS + 10000 + 2000));
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_TRUE(r.rep().ending == Ending::Fault);
    TEST_ASSERT_TRUE(r.rep().endingText.find("the roof was closed, as the start said") != std::string::npos);
    TEST_ASSERT_TRUE(r.rep().endingText.find("roof.openDirection") != std::string::npos);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 1850.0f, r.plant.positionMm);
}

// The owner's roof: heavy, so 40 % duty gives well under 40 % of the full
// speed. Timed as proportional to the duty, a close from an unknown position
// near the open end times out; the run measures the slow speed and, with the
// proposal saved, the same close fits its timeout.
void test_a_heavy_roof_measures_its_slow_speed_and_unknown_moves_then_fit(void)
{
    Roof::Settings s;
    s.speedMmPerSecond = 40.0f;
    const float deadband = 0.2258f; // 40 % duty: 9 mm/s
    {
        Rig before(1800.0f, s);
        before.plant.speedMmPerSecond = 40.0f;
        before.plant.deadbandFraction = deadband;
        TEST_ASSERT_TRUE(before.roof.command(Roof::Command::Close, Roof::Origin::Person).accepted);
        before.steps(250000);
        TEST_ASSERT_TRUE(before.roof.status().fault.kind == FaultKind::Timeout);
    }
    Rig r(0.0f, s);
    r.plant.speedMmPerSecond = 40.0f;
    r.plant.deadbandFraction = deadband;
    r.limitMs = MAX_LIMIT_MS;
    TEST_ASSERT_TRUE(r.start(Where::Closed, true).accepted);
    TEST_ASSERT_TRUE(r.finish(3000000));
    TEST_ASSERT_TRUE(r.rep().state == RunState::Done);
    const Proposal &p = r.rep().proposal;
    TEST_ASSERT_TRUE(p.speed);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 9.0f, p.slowSpeedMmPerSecond);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 40.0f, p.speedMmPerSecond);
    Roof::Settings now = s;
    now.travelMm = p.travelMm;
    now.speedMmPerSecond = p.speedMmPerSecond;
    now.slowSpeedMmPerSecond = p.slowSpeedMmPerSecond;
    now.timeoutMarginMs = p.timeoutMarginSeconds * 1000;
    Rig after(1800.0f, now);
    after.plant.speedMmPerSecond = 40.0f;
    after.plant.deadbandFraction = deadband;
    TEST_ASSERT_TRUE(after.roof.command(Roof::Command::Close, Roof::Origin::Person).accepted);
    after.steps(250000);
    TEST_ASSERT_TRUE(after.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(after.roof.status().state == State::Closed);
    // A full open and close from the closed end fit theirs too.
    TEST_ASSERT_TRUE(after.roof.command(Roof::Command::Open, Roof::Origin::Person).accepted);
    after.steps(200000);
    TEST_ASSERT_TRUE(after.roof.status().state == State::Open);
    TEST_ASSERT_TRUE(after.roof.command(Roof::Command::Close, Roof::Origin::Person).accepted);
    after.steps(200000);
    TEST_ASSERT_TRUE(after.roof.status().state == State::Closed);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_start_position_takes_its_legs);
    RUN_TEST(test_a_start_that_says_closed_at_the_open_end_fails_safely_and_says_why);
    RUN_TEST(test_a_heavy_roof_measures_its_slow_speed_and_unknown_moves_then_fit);
    RUN_TEST(test_top_legs_arrive_at_the_slow_duty_on_an_affine_motor);
    RUN_TEST(test_the_slow_close_waits_for_the_open_end_to_be_confirmed);
    RUN_TEST(test_no_confirmation_ends_the_run);
    RUN_TEST(test_a_backwards_motor_from_part_way_is_caught_at_the_confirmation);
    RUN_TEST(test_a_wrong_answer_and_a_backwards_motor_are_caught_too);
    RUN_TEST(test_top_legs_arrive_slow_with_a_large_deadband);
    return UNITY_END();
}
