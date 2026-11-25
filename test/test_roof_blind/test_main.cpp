// Spec 030 (constitution 8.0.0, I): the automatic close from an unknown
// position (slow, to the first magnet, like a person's) and the blind close
// (lib/RoofLogic, RoofControllerBlind.cpp). With the Hall sensor dead (a Hall rail fault
// latched) the automatic close may drive towards closed once, on time alone,
// at the slow duty: it stops at the unknown-position timeout, on a stall seen
// as supply sag, on Stop or Abort and on every fault the controller checks;
// the Hall rail fault stays latched.

#include <cmath>
#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "FaultRecord.h"
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

    // A roof part-open at `mm` whose Hall sensor has failed to the high rail
    // at rest: the Hall rail fault is latched.
    void deadHall(Bench &b, float mm = 900.0f)
    {
        b.sim.positionMm = mm;
        b.steps(20);
        b.sim.railCounts = 4095;
        b.steps(10);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
        TEST_ASSERT_TRUE(b.state() == State::Error);
    }

    // Starts the blind close and steps through its dead time: driving after.
    void startBlind(Bench &b)
    {
        TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).accepted);
        b.steps(static_cast<int>(DEAD_TIME_MS / b.stepMs) + 1);
        TEST_ASSERT_TRUE(b.roof.status().driving);
    }

    uint32_t drivenFor(Bench &b, uint32_t maxMs)
    {
        uint32_t ms = 0;
        while (b.roof.status().blind && ms < maxMs)
        {
            b.step();
            ms += b.stepMs;
        }
        return ms;
    }
} // namespace

void test_automatic_close_from_unknown_is_slow_and_stops_at_the_first_magnet(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_TRUE(b.roof.check(Command::Close, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_TRUE(b.roof.status().fromUnknown);
    b.step();
    TEST_ASSERT_EQUAL_UINT32(unknownMoveTimeoutMs(Settings()), b.roof.status().moveTimeoutMs);
    while (b.state() == State::Closing)
        TEST_ASSERT_TRUE(b.step().duty <= SLOW_DUTY);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.sim.atClosedMagnet());
}

void test_automatic_close_from_unknown_to_the_wrong_magnet_faults(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.reversedWiring = true;
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::WrongMagnet);
}

void test_only_the_automatic_close_with_a_hall_rail_fault(void)
{
    Bench b(Settings(), 900.0f);
    TEST_ASSERT_FALSE(b.roof.blindCloseReady()); // no fault
    TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).reason == Refusal::BlindUnavailable);
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.blindCloseReady());
    TEST_ASSERT_TRUE(b.roof.status().blindReady);
    for (Origin o : {Origin::Person, Origin::Client, Origin::Mqtt})
        TEST_ASSERT_TRUE(b.roof.blindClose(o).reason == Refusal::BlindUnavailable);
    TEST_ASSERT_TRUE(b.state() == State::Error);
    // Every other command is refused as for any latched fault.
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).reason == Refusal::FaultLatched);
}

void test_any_other_fault_never_drives(void)
{
    Bench b(Settings(), 900.0f);
    b.roof.raiseFault(Fault{FaultKind::Timeout, 1.0f, 2.0f});
    TEST_ASSERT_FALSE(b.roof.blindCloseReady());
    TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).reason == Refusal::BlindUnavailable);
    b.steps(100);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_off_by_the_setting(void)
{
    Settings s;
    s.blindClose = false;
    Bench b(s, 900.0f);
    deadHall(b);
    TEST_ASSERT_FALSE(b.roof.blindCloseReady());
    TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).reason == Refusal::BlindUnavailable);
}

void test_drives_closed_at_the_slow_duty_after_the_dead_time_and_reports_closing(void)
{
    Bench b(Settings(), 900.0f);
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).accepted);
    const Status &st = b.roof.status();
    TEST_ASSERT_TRUE(st.state == State::Closing);
    TEST_ASSERT_TRUE(st.target == End::Closed);
    TEST_ASSERT_TRUE(st.fromUnknown && st.blind);
    TEST_ASSERT_FALSE(st.blindReady);
    TEST_ASSERT_EQUAL_UINT32(unknownMoveTimeoutMs(Settings()), st.moveTimeoutMs);
    TEST_ASSERT_TRUE(b.roof.blindDone()); // counted from the request
    // A full dead time, even after a long rest.
    for (uint32_t t = 0; t < DEAD_TIME_MS; t += b.stepMs)
        TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    const Output out = b.step();
    TEST_ASSERT_TRUE(out.command == Driver::Command::Reverse); // open is Forward
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, out.duty);
    TEST_ASSERT_TRUE(st.driving);
    // The Hall stays at the rail: the fault stays latched, the drive goes on.
    b.steps(100);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_EQUAL_UINT8(SLOW_DUTY, b.roof.output().duty);
    TEST_ASSERT_TRUE(b.sim.positionMm < 900.0f);
}

void test_stops_at_the_limit_not_a_new_fault_and_never_again(void)
{
    Bench b(Settings(), 900.0f);
    deadHall(b);
    startBlind(b);
    const uint32_t limitMs = unknownMoveTimeoutMs(Settings());
    const uint32_t ms = drivenFor(b, limitMs + 1000);
    TEST_ASSERT_TRUE(ms <= limitMs + 40);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_FALSE(b.roof.status().positionKnown);
    TEST_ASSERT_EQUAL_UINT32(1, b.roof.blindReport().ended);
    TEST_ASSERT_TRUE(b.roof.blindReport().why == BlindEnd::Limit);
    TEST_ASSERT_TRUE(b.roof.blindReport().drivenMs >= limitMs);
    TEST_ASSERT_EQUAL_FLOAT(12.6f, b.roof.blindReport().restVolts);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.sim.positionMm); // against the closed end stop
    TEST_ASSERT_FALSE(b.roof.blindCloseReady());
    TEST_ASSERT_TRUE(b.roof.blindClose(Origin::Automatic).reason == Refusal::BlindUnavailable);
    b.steps(200);
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
}

void test_running_sag_is_not_a_stall(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.sagVolts = 1.1f / 0.4f; // the measured 1.1 V at the slow duty
    deadHall(b);
    startBlind(b);
    b.steps(500); // 10 s
    TEST_ASSERT_TRUE(b.roof.status().blind);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
}

void test_a_stall_sag_held_500_ms_stops_it(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.supplyVolts = 13.8f; // a charged battery: the stall sag stays above the supply minimum
    deadHall(b);
    startBlind(b);
    b.steps(100); // 2 s
    b.sim.sagVolts = 2.2f / 0.4f;
    const uint32_t ms = drivenFor(b, 2000);
    TEST_ASSERT_TRUE(ms >= BLIND_STALL_MS && ms <= BLIND_STALL_MS + 60);
    TEST_ASSERT_TRUE(b.roof.blindReport().why == BlindEnd::Stall);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.2f, b.roof.blindReport().deepestSag);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
    TEST_ASSERT_TRUE(b.state() == State::Error);
}

void test_the_start_up_surge_and_a_short_sag_are_not_judged(void)
{
    Bench b(Settings(), 900.0f);
    b.sim.supplyVolts = 13.8f;
    b.sim.sagVolts = 2.5f / 0.4f; // a deep surge for the first second
    deadHall(b);
    startBlind(b);
    b.steps(static_cast<int>(BLIND_SURGE_MS / b.stepMs) - 2);
    b.sim.sagVolts = 0.0f;
    b.steps(50);
    TEST_ASSERT_TRUE(b.roof.status().blind);
    b.sim.sagVolts = 2.5f / 0.4f; // 400 ms: shorter than the stall time
    b.steps(static_cast<int>(400 / b.stepMs));
    b.sim.sagVolts = 0.0f;
    b.steps(100);
    TEST_ASSERT_TRUE(b.roof.status().blind);
}

void test_a_stop_an_abort_or_a_persons_clear_ends_it(void)
{
    for (int how = 0; how < 3; ++how)
    {
        Bench b(Settings(), 900.0f);
        deadHall(b);
        startBlind(b);
        b.steps(50);
        if (how == 0)
            TEST_ASSERT_TRUE(b.command(Command::Stop, Origin::Person).accepted);
        else if (how == 1)
            TEST_ASSERT_TRUE(b.command(Command::Abort, Origin::Automatic).accepted);
        else
            TEST_ASSERT_TRUE(b.roof.clearFault(Origin::Person).accepted);
        TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off); // at once
        TEST_ASSERT_FALSE(b.roof.status().blind);
        TEST_ASSERT_TRUE(b.roof.blindReport().why == BlindEnd::Stopped);
        if (how < 2)
        {
            TEST_ASSERT_TRUE(b.state() == State::Error);
            TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
        }
        b.steps(10);
        TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    }
}

void test_a_supply_fault_a_late_step_or_a_raised_fault_ends_it(void)
{
    for (int how = 0; how < 3; ++how)
    {
        Bench b(Settings(), 900.0f);
        deadHall(b);
        startBlind(b);
        b.steps(50);
        if (how == 0)
        {
            b.sim.supplyVolts = 10.0f;
            b.steps(20);
        }
        else if (how == 1)
        {
            b.stepMs = MAX_STEP_GAP_MS + 1;
            b.step();
        }
        else
            b.roof.raiseFault(Fault{FaultKind::External, 1.0f, 0.0f}); // the park interlock
        TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
        TEST_ASSERT_FALSE(b.roof.status().blind);
        TEST_ASSERT_TRUE(b.roof.blindReport().why == BlindEnd::Fault);
        const FaultKind ended = how == 0 ? FaultKind::SupplyLow : how == 1 ? FaultKind::LateStep : FaultKind::External;
        TEST_ASSERT_TRUE(b.roof.blindReport().fault.kind == ended);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail); // the first fault is kept
        TEST_ASSERT_TRUE(b.state() == State::Error);
    }
}

void test_a_hall_that_recovers_meanwhile_does_not_clear_the_fault(void)
{
    Bench b(Settings(), 900.0f);
    deadHall(b);
    TEST_ASSERT_TRUE(b.roof.faultClearsAtRest());
    startBlind(b);
    b.sim.railCounts = -1;
    b.steps(static_cast<int>(HALL_REST_CLEAR_MS / b.stepMs) + 10);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::HallRail);
}

void test_settings_wait_and_the_done_flag_is_restored(void)
{
    Bench b(Settings(), 900.0f);
    deadHall(b);
    startBlind(b);
    TEST_ASSERT_TRUE(b.roof.applySettings(Settings()).reason == Refusal::Moving);
    // A restart: the fault and its record's done flag come back.
    Controller fresh;
    fresh.raiseFault(Fault{FaultKind::HallRail, 4095.0f, 4065.0f});
    TEST_ASSERT_TRUE(fresh.blindCloseReady());
    fresh.restoreBlindDone();
    TEST_ASSERT_TRUE(fresh.blindDone());
    TEST_ASSERT_FALSE(fresh.blindCloseReady());
    TEST_ASSERT_TRUE(fresh.clearFault(Origin::Person).accepted);
    TEST_ASSERT_FALSE(fresh.blindDone()); // one per latched fault
}

void test_the_fault_record_keeps_the_done_flag(void)
{
    using namespace SQM::RoofIo;
    FaultRecord rail;
    rail.fault = Fault{FaultKind::HallRail, 4095.0f, 4065.0f};
    rail.blindDone = true;
    uint8_t bytes[FAULT_RECORD_BYTES];
    encodeFault(rail, bytes);
    FaultRecord out;
    TEST_ASSERT_EQUAL(Decoded::Fault, decodeFault(bytes, sizeof(bytes), out));
    TEST_ASSERT_TRUE(out.blindDone);
    TEST_ASSERT_FALSE(out.atRest);
    rail.atRest = true;
    encodeFault(rail, bytes);
    TEST_ASSERT_EQUAL(Decoded::Fault, decodeFault(bytes, sizeof(bytes), out));
    TEST_ASSERT_TRUE(out.blindDone && out.atRest);
    // A record from before spec 030 (byte 5 at most 1) reads as not done.
    rail.blindDone = false;
    encodeFault(rail, bytes);
    TEST_ASSERT_EQUAL(1, bytes[5]);
    TEST_ASSERT_EQUAL(Decoded::Fault, decodeFault(bytes, sizeof(bytes), out));
    TEST_ASSERT_FALSE(out.blindDone);
    // Only a Hall rail fault carries it.
    FaultRecord timeout;
    timeout.fault = Fault{FaultKind::Timeout, 1.0f, 2.0f};
    timeout.blindDone = true;
    encodeFault(timeout, bytes);
    TEST_ASSERT_EQUAL(0, bytes[5]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_automatic_close_from_unknown_is_slow_and_stops_at_the_first_magnet);
    RUN_TEST(test_automatic_close_from_unknown_to_the_wrong_magnet_faults);
    RUN_TEST(test_only_the_automatic_close_with_a_hall_rail_fault);
    RUN_TEST(test_any_other_fault_never_drives);
    RUN_TEST(test_off_by_the_setting);
    RUN_TEST(test_drives_closed_at_the_slow_duty_after_the_dead_time_and_reports_closing);
    RUN_TEST(test_stops_at_the_limit_not_a_new_fault_and_never_again);
    RUN_TEST(test_running_sag_is_not_a_stall);
    RUN_TEST(test_a_stall_sag_held_500_ms_stops_it);
    RUN_TEST(test_the_start_up_surge_and_a_short_sag_are_not_judged);
    RUN_TEST(test_a_stop_an_abort_or_a_persons_clear_ends_it);
    RUN_TEST(test_a_supply_fault_a_late_step_or_a_raised_fault_ends_it);
    RUN_TEST(test_a_hall_that_recovers_meanwhile_does_not_clear_the_fault);
    RUN_TEST(test_settings_wait_and_the_done_flag_is_restored);
    RUN_TEST(test_the_fault_record_keeps_the_done_flag);
    return UNITY_END();
}
