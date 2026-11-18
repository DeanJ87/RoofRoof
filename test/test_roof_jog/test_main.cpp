// Spec 019: a person's jog in the roof controller (lib/RoofLogic,
// RoofControllerCalibrate.cpp, research R1-R3): its refusals, its direction
// and duty, the keep-alive deadline kept by the step's clock, its hard timeout
// and the drive budget, its end at the first magnet after leaving the start
// one, the existing faults, and the position it leaves.

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
    const uint8_t SLOW_DUTY = toDuty(0.40f);
    const int HOLD_STEPS = static_cast<int>(JOG_HOLD_MS / 20);

    void assertRefused(Refusal reason, const CommandResult &result)
    {
        TEST_ASSERT_FALSE(result.accepted);
        TEST_ASSERT_EQUAL_STRING(refusalName(reason), refusalName(result.reason));
    }

    // Steps `count` times, renewing the jog every `every` steps (as the page does).
    void holdFor(Bench &b, int count, int every = 7)
    {
        for (int i = 0; i < count; ++i)
        {
            if (i % every == 0)
                b.roof.holdJog(b.roof.jogId());
            b.step();
        }
    }

    // Holds the jog until it ends (or `maxMs` passes); true when it ended.
    bool holdUntilEnded(Bench &b, uint32_t maxMs)
    {
        for (uint32_t waited = 0; waited < maxMs; waited += b.stepMs)
        {
            b.roof.holdJog(b.roof.jogId());
            b.step();
            if (!b.roof.status().jogging)
                return true;
        }
        return false;
    }

    Settings shortRoof()
    {
        Settings s;
        s.travelMm = 400;
        return s;
    }
} // namespace

// --- Refusals -------------------------------------------------------------------

void test_only_a_person_may_jog(void)
{
    Bench b;
    assertRefused(Refusal::NotAPerson, b.roof.jog(End::Open, Origin::Client));
    assertRefused(Refusal::NotAPerson, b.roof.jog(End::Open, Origin::Mqtt));
    assertRefused(Refusal::NotAPerson, b.roof.jog(End::Open, Origin::Automatic));
    assertRefused(Refusal::NotAPerson, b.roof.checkJog(End::Closed, Origin::Client));
    TEST_ASSERT_EQUAL_UINT32(0, b.roof.jogId());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_a_jog_is_refused_with_a_fault_while_moving_or_not_ready(void)
{
    Bench faulted;
    faulted.roof.raiseFault(Fault{FaultKind::External, 0, 0});
    assertRefused(Refusal::FaultLatched, faulted.roof.jog(End::Open, Origin::Person));

    Bench moving;
    TEST_ASSERT_TRUE(moving.command(Command::Open).accepted);
    assertRefused(Refusal::Moving, moving.roof.jog(End::Open, Origin::Person));

    Bench jogging;
    TEST_ASSERT_TRUE(jogging.roof.jog(End::Open, Origin::Person).accepted);
    assertRefused(Refusal::Moving, jogging.roof.jog(End::Closed, Origin::Person));
    assertRefused(Refusal::Moving, jogging.roof.jog(End::Open, Origin::Person));

    Bench low;
    low.sim.supplyVolts = 10.0f;
    low.steps(2);
    assertRefused(Refusal::SupplyLow, low.roof.jog(End::Closed, Origin::Person));
}

void test_open_and_close_never_take_over_a_jog(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    for (Origin origin : {Origin::Person, Origin::Automatic, Origin::Client, Origin::Mqtt})
    {
        assertRefused(Refusal::Moving, b.roof.check(Command::Open, origin));
        assertRefused(Refusal::Moving, b.roof.command(Command::Open, origin));
        assertRefused(Refusal::Moving, b.roof.command(Command::Close, origin));
    }
    TEST_ASSERT_TRUE(b.roof.status().jogging);
}

// --- Direction and duty ---------------------------------------------------------

void test_a_jog_drives_at_the_slow_duty_toward_its_end(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    TEST_ASSERT_EQUAL_UINT32(1, b.roof.jogId());
    holdFor(b, 10);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Forward);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.roof.output().duty);
    TEST_ASSERT_TRUE(b.state() == State::Opening);
    TEST_ASSERT_TRUE(b.roof.status().jogging);
    TEST_ASSERT_TRUE(b.roof.status().calibrating); // the interlock watches it either way
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);

    Bench open(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(open.roof.jog(End::Closed, Origin::Person).accepted);
    TEST_ASSERT_EQUAL_UINT32(1, open.roof.jogId());
    holdFor(open, 10);
    TEST_ASSERT_TRUE(open.roof.output().command == Driver::Command::Reverse);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, open.roof.output().duty);
}

void test_the_duty_stays_slow_for_the_whole_jog(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    for (int i = 0; i < 500; ++i)
    {
        b.roof.holdJog(b.roof.jogId());
        b.step();
        TEST_ASSERT_TRUE(b.roof.output().duty <= SLOW_DUTY);
    }
}

// --- The keep-alive -------------------------------------------------------------

void test_a_jog_not_renewed_stops_after_the_hold_time(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    b.step(); // stamps the deadline: this step's time + JOG_HOLD_MS
    for (int i = 1; i < HOLD_STEPS; ++i)
    {
        b.step();
        TEST_ASSERT_TRUE(b.roof.status().jogging);
        TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.roof.output().duty);
    }
    b.step(); // JOG_HOLD_MS after the stamp
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None); // a stop, not a fault
}

void test_a_renewal_moves_the_deadline_and_needs_the_jogs_id(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    const uint32_t id = b.roof.jogId();
    TEST_ASSERT_FALSE(b.roof.holdJog(id + 1));
    TEST_ASSERT_FALSE(b.roof.holdJog(0));
    holdFor(b, 150, HOLD_STEPS - 1); // 3 s, renewed just inside the deadline
    TEST_ASSERT_TRUE(b.roof.status().jogging);
    // Wrong ids don't renew it: it stops on time.
    for (int i = 0; i < HOLD_STEPS + 1; ++i)
    {
        b.roof.holdJog(id + 1);
        b.step();
    }
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_FALSE(b.roof.holdJog(id)); // ended: never restarts it
    b.steps(10);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_a_renewal_between_steps_is_stamped_by_the_next_step(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    b.step();
    b.steps(HOLD_STEPS - 1); // last step before the deadline
    TEST_ASSERT_TRUE(b.roof.holdJog(b.roof.jogId()));
    // The next step stamps it: JOG_HOLD_MS from there, at most one step more
    // than from the renewal itself.
    b.steps(HOLD_STEPS);
    TEST_ASSERT_TRUE(b.roof.status().jogging);
    b.step();
    TEST_ASSERT_FALSE(b.roof.status().jogging);
}

void test_the_deadline_runs_through_the_dead_time(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, 20);
    TEST_ASSERT_TRUE(b.command(Command::Stop).accepted);
    // A new jog straight after the stop waits out the dead time; not renewed,
    // it ends before the driver comes on.
    TEST_ASSERT_TRUE(b.roof.jog(End::Closed, Origin::Person).accepted);
    for (int i = 0; i < HOLD_STEPS + 5; ++i)
    {
        b.step();
        TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    }
    TEST_ASSERT_FALSE(b.roof.status().jogging);
}

// --- Limits ---------------------------------------------------------------------

void test_the_jog_limit_is_the_smaller_of_its_cap_and_an_unknown_move(void)
{
    Bench b;
    TEST_ASSERT_EQUAL_UINT32(JOG_MAX_MS, b.roof.jogLimitMs());
    Settings s;
    s.travelMm = 200; // 200 mm at 15.2 mm/s plus 15 s: 28.2 s
    Bench small(s);
    TEST_ASSERT_EQUAL_UINT32(unknownMoveTimeoutMs(s), small.roof.jogLimitMs());
    TEST_ASSERT_TRUE(small.roof.jogLimitMs() < JOG_MAX_MS);
}

void test_a_jog_held_past_its_limit_is_a_timeout_fault(void)
{
    Bench b(Settings(), 300.0f);
    b.sim.openMagnetMissing = true;
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    TEST_ASSERT_TRUE(holdUntilEnded(b, JOG_MAX_MS + 2000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::Timeout);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(JOG_MAX_MS), b.roof.status().fault.limit);
    TEST_ASSERT_TRUE(b.roof.status().fault.measured >= static_cast<float>(JOG_MAX_MS));
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_repeated_jogs_are_bounded_by_the_drive_budget(void)
{
    // A 400 mm roof: budget = 400 / 15.2 + 15 = 41.3 s; two 25 s jogs exceed it.
    Bench b(shortRoof(), 200.0f);
    b.sim.openMagnetMissing = true;
    const uint32_t budget = driveBudgetMs(shortRoof());
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, 1250, 5);
    TEST_ASSERT_TRUE(b.command(Command::Stop).accepted);
    b.steps(30);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    TEST_ASSERT_TRUE(holdUntilEnded(b, 30000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::DriveBudget);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(budget), b.roof.status().fault.limit);
}

// --- Where it stops -------------------------------------------------------------

void test_a_jog_stops_at_the_first_magnet_after_leaving_its_start(void)
{
    Bench b(Settings(), 1700.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    TEST_ASSERT_TRUE(holdUntilEnded(b, 20000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
}

void test_a_jog_never_raises_the_wrong_magnet_fault(void)
{
    // The closed end reads high but the settings say open: the jog toward
    // "open" leaves the closed magnet and stops on the other one (polarity
    // is what is being taught).
    Bench b;
    b.sim.highEndClosed = true;
    b.settleHall();
    TEST_ASSERT_TRUE(b.state() == State::Open); // as the wrong polarity reads it
    // A full travel takes several presses, each released before its limit.
    for (int i = 0; i < 8 && !b.sim.atOpenMagnet(); ++i)
    {
        TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
        holdFor(b, 1250); // 25 s
        b.command(Command::Stop);
        b.steps(30);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    }
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(b.sim.atOpenMagnet());
    TEST_ASSERT_TRUE(b.state() == State::Closed); // the wrong polarity's name for it
}

void test_a_jog_into_its_own_magnets_end_is_bounded_by_the_leave_time(void)
{
    Bench b; // closed, on the closed magnet, against the end stop
    TEST_ASSERT_TRUE(b.roof.jog(End::Closed, Origin::Person).accepted);
    TEST_ASSERT_TRUE(holdUntilEnded(b, 20000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_EQUAL_FLOAT(10000.0f, b.roof.status().fault.limit);
}

void test_a_jog_stopped_part_way_leaves_the_position_unknown(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, 150);
    TEST_ASSERT_TRUE(b.sim.positionMm > 30.0f);
    TEST_ASSERT_TRUE(b.command(Command::Stop).accepted);
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_FALSE(b.roof.status().calibrating);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    b.steps(5);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
}

void test_abort_and_faults_end_a_jog(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, 10);
    TEST_ASSERT_TRUE(b.command(Command::Abort, Origin::Client).accepted);
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);

    Bench f(Settings(), 900.0f);
    TEST_ASSERT_TRUE(f.roof.jog(End::Closed, Origin::Person).accepted);
    holdFor(f, 10);
    f.roof.raiseFault(Fault{FaultKind::External, 1, 0}); // the park interlock, say
    TEST_ASSERT_FALSE(f.roof.status().jogging);
    TEST_ASSERT_TRUE(f.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_FALSE(f.roof.holdJog(f.roof.jogId()));
}

void test_a_low_supply_stops_a_jog_as_a_fault(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, 10);
    b.sim.supplyVolts = 10.0f;
    TEST_ASSERT_TRUE(holdUntilEnded(b, 2000));
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::SupplyLow);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_a_person_may_jog);
    RUN_TEST(test_a_jog_is_refused_with_a_fault_while_moving_or_not_ready);
    RUN_TEST(test_open_and_close_never_take_over_a_jog);
    RUN_TEST(test_a_jog_drives_at_the_slow_duty_toward_its_end);
    RUN_TEST(test_the_duty_stays_slow_for_the_whole_jog);
    RUN_TEST(test_a_jog_not_renewed_stops_after_the_hold_time);
    RUN_TEST(test_a_renewal_moves_the_deadline_and_needs_the_jogs_id);
    RUN_TEST(test_a_renewal_between_steps_is_stamped_by_the_next_step);
    RUN_TEST(test_the_deadline_runs_through_the_dead_time);
    RUN_TEST(test_the_jog_limit_is_the_smaller_of_its_cap_and_an_unknown_move);
    RUN_TEST(test_a_jog_held_past_its_limit_is_a_timeout_fault);
    RUN_TEST(test_repeated_jogs_are_bounded_by_the_drive_budget);
    RUN_TEST(test_a_jog_stops_at_the_first_magnet_after_leaving_its_start);
    RUN_TEST(test_a_jog_never_raises_the_wrong_magnet_fault);
    RUN_TEST(test_a_jog_into_its_own_magnets_end_is_bounded_by_the_leave_time);
    RUN_TEST(test_a_jog_stopped_part_way_leaves_the_position_unknown);
    RUN_TEST(test_abort_and_faults_end_a_jog);
    RUN_TEST(test_a_low_supply_stops_a_jog_as_a_fault);
    return UNITY_END();
}
