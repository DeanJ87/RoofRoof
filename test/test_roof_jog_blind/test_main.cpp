// Spec 033 (constitution 9.0.0, I): a person's jog with the Hall sensor
// failed (lib/RoofLogic, RoofControllerCalibrate.cpp and RoofControllerBlind.cpp).
// With the Hall rail fault latched (and no other), or the reading at a rail or
// not settled, a person's jog runs blind: at the slow duty, toward the end the
// person holds, while it is renewed, up to the jog's limit, on time alone. The
// fault stays latched and the position unknown. Other origins and other
// faults are refused as before.

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
    const int DEAD_STEPS = static_cast<int>(DEAD_TIME_MS / 20) + 1;

    void assertRefused(Refusal reason, const CommandResult &result)
    {
        TEST_ASSERT_FALSE(result.accepted);
        TEST_ASSERT_EQUAL_STRING(refusalName(reason), refusalName(result.reason));
    }

    // A roof part-open at `mm` whose Hall sensor has failed to the high rail.
    void deadHall(Bench &b, float mm = 900.0f)
    {
        b.sim.positionMm = mm;
        b.steps(20);
        b.sim.railCounts = 4095;
        b.steps(10);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    }

    // Steps `count` times, renewing the jog every 7 steps (as the page does).
    void holdFor(Bench &b, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            if (i % 7 == 0)
                b.roof.holdJog(b.roof.jogId());
            b.step();
        }
    }
} // namespace

void test_a_person_may_jog_either_way_with_the_hall_rail_fault(void)
{
    for (End target : {End::Open, End::Closed})
    {
        Bench b;
        deadHall(b);
        const float before = b.sim.positionMm;
        TEST_ASSERT_TRUE(b.roof.checkJog(target, Origin::Person).accepted);
        TEST_ASSERT_TRUE(b.roof.jog(target, Origin::Person).accepted);
        TEST_ASSERT_TRUE(b.roof.status().jogging);
        TEST_ASSERT_FALSE(b.roof.status().blind); // not the blind close
        holdFor(b, DEAD_STEPS + 20);
        const Driver::Command expected = target == End::Open ? Driver::Command::Forward : Driver::Command::Reverse;
        TEST_ASSERT_TRUE(b.roof.output().command == expected);
        TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.roof.output().duty);
        TEST_ASSERT_TRUE(b.state() == (target == End::Open ? State::Opening : State::Closing));
        TEST_ASSERT_TRUE(target == End::Open ? b.sim.positionMm > before : b.sim.positionMm < before);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
        TEST_ASSERT_FALSE(b.roof.status().positionKnown);
    }
}

void test_other_origins_are_still_refused(void)
{
    Bench b;
    deadHall(b);
    for (Origin origin : {Origin::Automatic, Origin::Client, Origin::Mqtt})
    {
        TEST_ASSERT_FALSE(b.roof.checkJog(End::Closed, origin).accepted);
        TEST_ASSERT_FALSE(b.roof.jog(End::Closed, origin).accepted);
    }
    TEST_ASSERT_EQUAL_UINT32(0, b.roof.jogId());
    // Open and Close stay refused for everyone while the fault is latched.
    assertRefused(Refusal::FaultLatched, b.command(Command::Close, Origin::Person));
    assertRefused(Refusal::FaultLatched, b.command(Command::Open, Origin::Client));
}

void test_a_blind_jog_not_renewed_stops_after_the_hold_time(void)
{
    Bench b;
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.jog(End::Closed, Origin::Person).accepted);
    holdFor(b, DEAD_STEPS + 5);
    TEST_ASSERT_TRUE(b.roof.status().driving);
    int steps = 0;
    while (b.roof.status().jogging && steps < 100)
    {
        b.step();
        ++steps;
    }
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_TRUE(steps * b.stepMs <= JOG_HOLD_MS + b.stepMs);
    TEST_ASSERT_EQUAL_UINT8(0, b.roof.output().duty);
    TEST_ASSERT_TRUE(b.state() == State::Error); // the Hall rail fault is still latched
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
    // The blind close's report is its own.
    TEST_ASSERT_EQUAL_UINT32(0, b.roof.blindReport().ended);
}

void test_a_held_blind_jog_stops_at_the_jog_limit(void)
{
    Settings s;
    s.travelMm = 20000; // a limit of JOG_MAX_MS, with the roof far from its ends
    Bench b(s);
    deadHall(b, 10000.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    TEST_ASSERT_EQUAL_UINT32(b.roof.jogLimitMs(), b.roof.status().moveTimeoutMs);
    uint32_t drivenMs = 0;
    int i = 0;
    while (b.roof.status().jogging && drivenMs < 2 * JOG_MAX_MS)
    {
        if (i++ % 7 == 0)
            b.roof.holdJog(b.roof.jogId());
        b.step();
        if (b.roof.status().driving)
            drivenMs += b.stepMs;
    }
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_UINT32_WITHIN(2 * b.stepMs, b.roof.jogLimitMs(), drivenMs);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_EQUAL_UINT8(0, b.roof.output().duty);
}

void test_release_stop_and_abort_end_a_blind_jog(void)
{
    Bench b;
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, DEAD_STEPS + 5);
    TEST_ASSERT_FALSE(b.roof.releaseJog(b.roof.jogId() + 1));
    TEST_ASSERT_TRUE(b.roof.status().jogging);
    TEST_ASSERT_TRUE(b.roof.releaseJog(b.roof.jogId()));
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_EQUAL_UINT8(0, b.roof.output().duty);
    TEST_ASSERT_FALSE(b.roof.holdJog(b.roof.jogId()));

    for (Command stop : {Command::Stop, Command::Abort})
    {
        b.steps(DEAD_STEPS);
        TEST_ASSERT_TRUE(b.roof.jog(End::Closed, Origin::Person).accepted);
        holdFor(b, DEAD_STEPS + 5);
        TEST_ASSERT_TRUE(b.roof.status().driving);
        TEST_ASSERT_TRUE(b.command(stop).accepted);
        TEST_ASSERT_FALSE(b.roof.status().jogging);
        TEST_ASSERT_EQUAL_UINT8(0, b.roof.output().duty);
    }
}

void test_other_faults_still_refuse_a_jog(void)
{
    const FaultKind kinds[] = {
        FaultKind::External,
        FaultKind::OverCurrent,
        FaultKind::SupplyLow,
        FaultKind::LateStep,
        FaultKind::Timeout,
        FaultKind::WrongMagnet,
        FaultKind::DriveBudget};
    for (FaultKind kind : kinds)
    {
        Bench b(Settings(), 900.0f);
        b.roof.raiseFault(Fault{kind, 0, 0});
        b.sim.railCounts = 4095; // the Hall dead too: the first fault is the one that counts
        b.steps(10);
        assertRefused(Refusal::FaultLatched, b.roof.jog(End::Closed, Origin::Person));
    }
}

void test_a_blind_jog_needs_valid_settings_and_power(void)
{
    Bench low;
    deadHall(low);
    low.sim.supplyVolts = 10.0f;
    low.steps(2);
    assertRefused(Refusal::SupplyLow, low.roof.jog(End::Closed, Origin::Person));

    Bench unreadable;
    deadHall(unreadable);
    unreadable.sim.supplyVolts = 0.0f;
    unreadable.steps(2);
    TEST_ASSERT_FALSE(unreadable.roof.jog(End::Closed, Origin::Person).accepted);

    Bench invalid;
    deadHall(invalid);
    Settings bad;
    bad.slowDutyPercent = 0; // outside its range
    TEST_ASSERT_TRUE(invalid.roof.applySettings(bad).accepted);
    assertRefused(Refusal::SettingsInvalid, invalid.roof.jog(End::Closed, Origin::Person));
}

void test_a_supply_drop_during_a_blind_jog_stops_it(void)
{
    Bench b;
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.jog(End::Closed, Origin::Person).accepted);
    holdFor(b, DEAD_STEPS + 5);
    b.sim.supplyVolts = 10.0f;
    holdFor(b, static_cast<int>(CONFIRM_MS / 20) + 3);
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_EQUAL_UINT8(0, b.roof.output().duty);
}

void test_a_blind_jog_is_never_taken_over_but_yields_to_the_blind_close(void)
{
    Settings s;
    s.blindClose = true;
    Bench b(s);
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person).accepted);
    holdFor(b, DEAD_STEPS + 5);
    assertRefused(Refusal::Moving, b.roof.jog(End::Closed, Origin::Person));
    assertRefused(Refusal::Moving, b.roof.checkJog(End::Closed, Origin::Person));
    // The automatic blind close takes over, as the automatic close takes over a jog (spec 019).
    TEST_ASSERT_TRUE(b.roof.blindCloseReady());
    TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).accepted);
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_TRUE(b.roof.status().blind);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_FALSE(b.roof.holdJog(b.roof.jogId()));
}

void test_a_person_may_jog_before_the_hall_has_settled(void)
{
    Controller roof;
    RoofTest::RoofSim sim;
    sim.positionMm = 900.0f;
    roof.tick(sim.readings(1000));
    TEST_ASSERT_TRUE(roof.status().hall == HallLevel::Unsettled);
    TEST_ASSERT_TRUE(roof.status().fault.kind == FaultKind::None);
    assertRefused(Refusal::HallNotSettled, roof.check(Command::Close, Origin::Person));
    TEST_ASSERT_FALSE(roof.jog(End::Closed, Origin::Client).accepted);
    TEST_ASSERT_TRUE(roof.jog(End::Closed, Origin::Person).accepted);
    assertRefused(Refusal::Moving, roof.command(Command::Close, Origin::Person));
    uint32_t now = 1000;
    for (int i = 0; i < 60; ++i)
    {
        if (i % 7 == 0)
            roof.holdJog(roof.jogId());
        sim.drive(roof.output(), 20);
        now += 20;
        roof.tick(sim.readings(now));
    }
    TEST_ASSERT_TRUE(roof.status().jogging);
    TEST_ASSERT_TRUE(roof.output().command == Driver::Command::Reverse);
    TEST_ASSERT_TRUE(roof.releaseJog(roof.jogId()));
    TEST_ASSERT_TRUE(roof.status().state == State::Unknown); // no fault latched
}

// --- Overriding the end magnets (a working Hall sensor) ---------------------------

namespace
{
    // Jogs toward open from 900 mm, held until the roof is well into the open
    // magnet's zone (or `ignoreMagnets` is off and it stops); true while it still jogs.
    bool jogIntoOpenMagnet(Bench &b, bool ignoreMagnets)
    {
        b.sim.positionMm = b.sim.travelMm - 120.0f;
        b.steps(30);
        TEST_ASSERT_TRUE(b.roof.jog(End::Open, Origin::Person, ignoreMagnets).accepted);
        int i = 0;
        while (b.roof.status().jogging && b.sim.positionMm < b.sim.travelMm - 5.0f && i < 3000)
        {
            if (i++ % 7 == 0)
                b.roof.holdJog(b.roof.jogId());
            b.step();
        }
        holdFor(b, 20); // the Hall has confirmed the open magnet by now
        return b.roof.status().jogging;
    }
} // namespace

void test_a_normal_jog_stops_at_the_end_magnet(void)
{
    Bench b;
    TEST_ASSERT_FALSE(jogIntoOpenMagnet(b, false));
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_FALSE(b.roof.magnetsDoubted());
}

void test_an_override_jog_carries_on_past_the_end_magnet(void)
{
    Bench b;
    TEST_ASSERT_TRUE(jogIntoOpenMagnet(b, true));
    TEST_ASSERT_TRUE(b.roof.status().hall == HallLevel::High); // the open magnet, read and ignored
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Forward);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.roof.output().duty);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(b.roof.releaseJog(b.roof.jogId()));
    b.steps(20);
    // The magnet read at rest doesn't place the roof until a move reaches one.
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
    TEST_ASSERT_TRUE(b.roof.magnetsDoubted());
    TEST_ASSERT_FALSE(b.roof.atEnd(Command::Open));
    assertRefused(Refusal::PositionUnknown, b.command(Command::Open, Origin::Client));
    TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Person).accepted);
    b.steps(DEAD_STEPS);
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.roof.status().positionKnown);
    TEST_ASSERT_FALSE(b.roof.magnetsDoubted());
}

void test_an_override_is_a_persons_only_and_any_fault_refuses_it(void)
{
    Bench b(Settings(), 900.0f);
    for (Origin origin : {Origin::Automatic, Origin::Client, Origin::Mqtt})
        assertRefused(Refusal::NotAPerson, b.roof.jog(End::Open, origin, true));
    b.roof.raiseFault(Fault{FaultKind::WrongMagnet, 0, 0});
    assertRefused(Refusal::FaultLatched, b.roof.jog(End::Open, Origin::Person, true));
    TEST_ASSERT_FALSE(b.roof.magnetsDoubted());
}

void test_an_override_jog_keeps_the_keep_alive_and_the_limit(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.jog(End::Closed, Origin::Person, true).accepted);
    TEST_ASSERT_EQUAL_UINT32(b.roof.jogLimitMs(), b.roof.status().moveTimeoutMs);
    holdFor(b, DEAD_STEPS + 5);
    TEST_ASSERT_TRUE(b.roof.status().driving);
    b.steps(static_cast<int>(JOG_HOLD_MS / 20) + 1);
    TEST_ASSERT_FALSE(b.roof.status().jogging);
    TEST_ASSERT_EQUAL_UINT8(0, b.roof.output().duty);
}

void test_new_settings_trust_the_magnets_again(void)
{
    Bench b;
    jogIntoOpenMagnet(b, true);
    b.roof.releaseJog(b.roof.jogId());
    b.steps(5);
    TEST_ASSERT_TRUE(b.roof.applySettings(Settings()).accepted);
    TEST_ASSERT_FALSE(b.roof.magnetsDoubted());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_person_may_jog_either_way_with_the_hall_rail_fault);
    RUN_TEST(test_other_origins_are_still_refused);
    RUN_TEST(test_a_blind_jog_not_renewed_stops_after_the_hold_time);
    RUN_TEST(test_a_held_blind_jog_stops_at_the_jog_limit);
    RUN_TEST(test_release_stop_and_abort_end_a_blind_jog);
    RUN_TEST(test_other_faults_still_refuse_a_jog);
    RUN_TEST(test_a_blind_jog_needs_valid_settings_and_power);
    RUN_TEST(test_a_supply_drop_during_a_blind_jog_stops_it);
    RUN_TEST(test_a_blind_jog_is_never_taken_over_but_yields_to_the_blind_close);
    RUN_TEST(test_a_person_may_jog_before_the_hall_has_settled);
    RUN_TEST(test_a_normal_jog_stops_at_the_end_magnet);
    RUN_TEST(test_an_override_jog_carries_on_past_the_end_magnet);
    RUN_TEST(test_an_override_is_a_persons_only_and_any_fault_refuses_it);
    RUN_TEST(test_an_override_jog_keeps_the_keep_alive_and_the_limit);
    RUN_TEST(test_new_settings_trust_the_magnets_again);
    return UNITY_END();
}
