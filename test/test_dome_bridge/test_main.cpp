// The roof as the Alpaca Dome (spec 006, data-model.md): the shutter state
// table, the action mapping, which stops stand spec 005 down, and the
// refusal text.

#include <unity.h>

#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "CommandCheck.h"
#include "DomeBridge.h"
#include "RoofController.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using Alpaca::DomeAction;
using Alpaca::ShutterState;
using Roof::Command;
using Roof::Origin;
using Roof::State;
using namespace SQM::Roof;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Alpaca::DomeStatus of(State state, bool closePending, bool running = true)
    {
        Roof::Status status;
        status.state = state;
        return RoofIo::domeStatus(status, closePending, running);
    }

    void expect(State state, bool closePending, ShutterState shutter, bool slewing)
    {
        const Alpaca::DomeStatus status = of(state, closePending);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(shutter), static_cast<int>(status.shutter));
        TEST_ASSERT_EQUAL(slewing, status.slewing);
    }
} // namespace

void test_shutter_numbers_are_ascom_s(void)
{
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(ShutterState::Open));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(ShutterState::Closed));
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(ShutterState::Opening));
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(ShutterState::Closing));
    TEST_ASSERT_EQUAL_INT(4, static_cast<int>(ShutterState::Error));
}

void test_shutter_state_without_a_waiting_close(void)
{
    expect(State::Closed, false, ShutterState::Closed, false);
    expect(State::Open, false, ShutterState::Open, false);
    expect(State::Stopped, false, ShutterState::Error, false); // part-way: neither open nor closed
    expect(State::Opening, false, ShutterState::Opening, true);
    expect(State::Closing, false, ShutterState::Closing, true);
    expect(State::Unknown, false, ShutterState::Error, false);
    expect(State::Error, false, ShutterState::Error, false);
}

void test_shutter_state_with_a_waiting_close(void)
{
    expect(State::Closed, true, ShutterState::Closed, false);
    expect(State::Open, true, ShutterState::Closing, true);
    expect(State::Stopped, true, ShutterState::Closing, true);
    expect(State::Opening, true, ShutterState::Opening, true);
    expect(State::Closing, true, ShutterState::Closing, true);
    expect(State::Unknown, true, ShutterState::Closing, true);
    expect(State::Error, true, ShutterState::Error, false);
}

void test_nothing_but_the_closed_magnet_reads_closed(void)
{
    for (State s : {State::Unknown, State::Open, State::Opening, State::Closing, State::Stopped, State::Error})
        for (bool pending : {false, true})
            TEST_ASSERT_TRUE(of(s, pending).shutter != ShutterState::Closed);
}

// A client waiting for "open" must not start imaging under a part-open roof.
void test_nothing_but_the_open_magnet_reads_open(void)
{
    for (State s : {State::Unknown, State::Closed, State::Opening, State::Closing, State::Stopped, State::Error})
        for (bool pending : {false, true})
            TEST_ASSERT_TRUE(of(s, pending).shutter != ShutterState::Open);
}

void test_a_controller_not_running_reads_error(void)
{
    for (State s : {State::Unknown, State::Closed, State::Open, State::Opening, State::Closing, State::Stopped, State::Error})
        for (bool pending : {false, true})
        {
            const Alpaca::DomeStatus status = of(s, pending, false);
            TEST_ASSERT_TRUE(status.shutter == ShutterState::Error);
            TEST_ASSERT_FALSE(status.slewing);
        }
}

void test_actions_map_to_roof_commands(void)
{
    TEST_ASSERT_TRUE(RoofIo::roofCommand(DomeAction::OpenShutter) == Command::Open);
    TEST_ASSERT_TRUE(RoofIo::roofCommand(DomeAction::CloseShutter) == Command::Close);
    // A stop, not an abort: the position estimate is kept.
    TEST_ASSERT_TRUE(RoofIo::roofCommand(DomeAction::AbortSlew) == Command::Stop);
    TEST_ASSERT_TRUE(RoofIo::roofCommand(static_cast<DomeAction>(99)) == Command::Stop);
}

// Constitution III: an unauthenticated client's stop stops the roof but can't
// stand the close on unsafe weather down; only a person's stop does.
void test_only_a_person_s_stop_counts(void)
{
    for (Command c : {Command::Stop, Command::Abort})
    {
        TEST_ASSERT_TRUE(RoofIo::countsAsPersonStop(c, Origin::Person));
        TEST_ASSERT_FALSE(RoofIo::countsAsPersonStop(c, Origin::Client));
        TEST_ASSERT_FALSE(RoofIo::countsAsPersonStop(c, Origin::Automatic));
    }
}

void test_moves_never_count_as_stops(void)
{
    for (Command c : {Command::Open, Command::Close})
        for (Origin o : {Origin::Person, Origin::Automatic, Origin::Client})
            TEST_ASSERT_FALSE(RoofIo::countsAsPersonStop(c, o));
}

void test_refusal_text_names_a_latched_fault(void)
{
    RoofIo::FaultRecord fault;
    fault.fault = Roof::Fault{Roof::FaultKind::SupplyLow, 10.9f, 11.5f};
    TEST_ASSERT_EQUAL_STRING(
        "A fault is latched: a person must clear it first: Supply 10.9 V < 11.5 V",
        RoofIo::domeRefusalText("faultLatched", "A fault is latched: a person must clear it first", fault).c_str());
}

void test_refusal_text_otherwise_unchanged(void)
{
    RoofIo::FaultRecord fault;
    fault.fault = Roof::Fault{Roof::FaultKind::SupplyLow, 10.9f, 11.5f};
    TEST_ASSERT_EQUAL_STRING(
        "Weather not safe: unsafe: rain", RoofIo::domeRefusalText("weatherNotSafe", "Weather not safe: unsafe: rain", fault).c_str());
    // A fault refusal with no fault recorded (it can't happen) keeps the text.
    TEST_ASSERT_EQUAL_STRING("A fault is latched", RoofIo::domeRefusalText("faultLatched", "A fault is latched", {}).c_str());
}

void test_answer_carries_the_refusal(void)
{
    RoofIo::FaultRecord fault;
    fault.fault = Roof::Fault{Roof::FaultKind::Timeout, 61000.0f, 60000.0f};
    Alpaca::DomeAnswer a =
        RoofIo::domeAnswer(Command::Open, false, "faultLatched", "A fault is latched: a person must clear it first", fault);
    TEST_ASSERT_FALSE(a.accepted);
    TEST_ASSERT_EQUAL_STRING(
        "A fault is latched: a person must clear it first: No target magnet after 61.0 s >= 60.0 s timeout", a.message.c_str());
    a = RoofIo::domeAnswer(Command::Close, false, "mountNotParked", "Mount not parked: park not confirmed", fault);
    TEST_ASSERT_FALSE(a.accepted);
    TEST_ASSERT_EQUAL_STRING("Mount not parked: park not confirmed", a.message.c_str());
    a = RoofIo::domeAnswer(Command::Close, true, "", "", fault);
    TEST_ASSERT_TRUE(a.accepted);
    TEST_ASSERT_EQUAL_STRING("", a.message.c_str());
}

// The driver is off already when the controller isn't running: the stop is done.
void test_a_stop_with_the_controller_down_is_done(void)
{
    for (Command c : {Command::Stop, Command::Abort})
    {
        const Alpaca::DomeAnswer a = RoofIo::domeAnswer(c, false, "notStarted", "The roof controller didn't start", {});
        TEST_ASSERT_TRUE(a.accepted);
    }
}

// ...but a stop the roof refused (a client's, during the close on unsafe weather) is reported.
void test_a_refused_stop_is_reported(void)
{
    const Alpaca::DomeAnswer a = RoofIo::domeAnswer(Command::Stop, false, "automaticClose", "The roof is closing on unsafe weather", {});
    TEST_ASSERT_FALSE(a.accepted);
    TEST_ASSERT_EQUAL_STRING("The roof is closing on unsafe weather", a.message.c_str());
}

void test_every_dome_action_is_the_client_s(void)
{
    for (DomeAction a : {DomeAction::OpenShutter, DomeAction::CloseShutter, DomeAction::AbortSlew})
    {
        const RoofIo::DomeDispatch d = RoofIo::domeDispatch(a);
        TEST_ASSERT_TRUE(d.origin == Origin::Client);
        TEST_ASSERT_TRUE(d.command == RoofIo::roofCommand(a));
    }
}

// The client origin (Alpaca): not a person (constitution I), through the real motion logic.
void test_client_moves_refused_when_unknown(void)
{
    Bench b(Settings(), 900.0f);
    for (Command c : {Command::Open, Command::Close})
    {
        const CommandResult checked = b.roof.check(c, Origin::Client);
        TEST_ASSERT_FALSE(checked.accepted);
        TEST_ASSERT_TRUE(checked.reason == Refusal::PositionUnknown);
        const CommandResult r = b.command(c, Origin::Client);
        TEST_ASSERT_FALSE(r.accepted);
        TEST_ASSERT_TRUE(r.reason == Refusal::PositionUnknown);
    }
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    for (int i = 0; i < 500; ++i)
        TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
}

void test_client_moves_from_a_known_position(void)
{
    Bench b;
    TEST_ASSERT_TRUE(b.roof.check(Command::Open, Origin::Client).accepted);
    TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Client).accepted);
    TEST_ASSERT_TRUE(b.state() == State::Opening);
}

void test_client_cannot_clear_a_fault(void)
{
    Bench b;
    b.roof.raiseFault(Fault{FaultKind::External, 1.0f, 0.0f});
    const CommandResult r = b.roof.clearFault(Origin::Client);
    TEST_ASSERT_FALSE(r.accepted);
    TEST_ASSERT_TRUE(r.reason == Refusal::NotAPerson);
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Client).reason == Refusal::FaultLatched);
}

// --- The command decision RoofControl and the simulator share (CommandCheck) ---

namespace
{
    RoofIo::GateInputs safeAndParked()
    {
        RoofIo::GateInputs g;
        g.weather = Safety::Verdict::Safe;
        g.weatherText = "safe";
        g.parked = true;
        g.parkText = "parked";
        return g;
    }

    // Runs `command` as RoofControl does: the shared check, then the logic.
    RoofIo::CommandContext ctx(bool closePending = false)
    {
        RoofIo::CommandContext c;
        c.closePending = closePending;
        return c;
    }

    bool run(Bench &b, Command command, Origin origin, const RoofIo::GateInputs &gates, const RoofIo::CommandContext &context = {})
    {
        const RoofIo::CommandCheck check = RoofIo::checkCommand(command, origin, gates, b.roof, context);
        if (!check.accepted)
            return false;
        return b.command(command, origin).accepted;
    }

    void expectStill(Bench &b)
    {
        for (int i = 0; i < 200; ++i)
            TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    }
} // namespace

void test_client_open_refused_while_unsafe(void)
{
    Bench b;
    RoofIo::GateInputs gates = safeAndParked();
    for (Safety::Verdict v : {Safety::Verdict::Unsafe, Safety::Verdict::Unknown})
    {
        gates.weather = v;
        gates.weatherText = "unsafe: rain";
        const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Open, Origin::Client, gates, b.roof, ctx());
        TEST_ASSERT_FALSE(check.accepted);
        TEST_ASSERT_EQUAL_STRING("weatherNotSafe", check.reason.c_str());
        TEST_ASSERT_EQUAL_STRING("Weather not safe: unsafe: rain", check.text.c_str());
        TEST_ASSERT_FALSE(run(b, Command::Open, Origin::Client, gates));
    }
    expectStill(b);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_client_close_refused_unless_parked(void)
{
    Bench b(Settings(), 1850.0f); // open
    RoofIo::GateInputs gates = safeAndParked();
    gates.parked = false;
    gates.parkText = "N.I.N.A. not responding";
    const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Close, Origin::Client, gates, b.roof, ctx());
    TEST_ASSERT_FALSE(check.accepted);
    TEST_ASSERT_EQUAL_STRING("mountNotParked", check.reason.c_str());
    TEST_ASSERT_EQUAL_STRING("Mount not parked: park not confirmed (N.I.N.A. not responding)", check.text.c_str());
    TEST_ASSERT_FALSE(run(b, Command::Close, Origin::Client, gates));
    // A fitted park switch that isn't closed refuses too, with the result saying parked.
    gates = safeAndParked();
    gates.fittedSwitches = 1;
    gates.switch1 = RoofIo::Contact::Open;
    TEST_ASSERT_FALSE(run(b, Command::Close, Origin::Client, gates));
    expectStill(b);
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

void test_client_moves_when_safe_and_parked(void)
{
    Bench b;
    const RoofIo::GateInputs gates = safeAndParked();
    TEST_ASSERT_TRUE(run(b, Command::Open, Origin::Client, gates));
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(run(b, Command::Close, Origin::Client, gates));
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

void test_client_unknown_position_refused_through_the_check(void)
{
    Bench b(Settings(), 900.0f);
    const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Open, Origin::Client, safeAndParked(), b.roof, ctx());
    TEST_ASSERT_FALSE(check.accepted);
    TEST_ASSERT_EQUAL_STRING("positionUnknown", check.reason.c_str());
    // A person may (constitution I).
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Open, Origin::Person, safeAndParked(), b.roof, ctx()).accepted);
}

void test_client_cannot_reverse_a_close(void)
{
    Bench b(Settings(), 1850.0f);
    const RoofIo::GateInputs gates = safeAndParked();
    TEST_ASSERT_TRUE(run(b, Command::Close, Origin::Person, gates));
    b.steps(50);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Open, Origin::Client, gates, b.roof, ctx());
    TEST_ASSERT_FALSE(check.accepted);
    TEST_ASSERT_EQUAL_STRING("closing", check.reason.c_str());
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    // ...nor replace a waiting close.
    Bench open(Settings(), 1850.0f);
    TEST_ASSERT_FALSE(RoofIo::checkCommand(Command::Open, Origin::Client, gates, open.roof, ctx(true)).accepted);
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Open, Origin::Client, gates, open.roof, ctx()).accepted);
    // A person still can (spec 003).
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Open, Origin::Person, gates, b.roof, ctx()).accepted);
}

// With nothing closing, every stop passes.
void test_stops_pass_every_check(void)
{
    Bench b(Settings(), 900.0f);
    RoofIo::GateInputs gates; // weather unknown, not parked
    b.roof.raiseFault(Fault{FaultKind::External, 1.0f, 0.0f});
    for (Command c : {Command::Stop, Command::Abort})
    {
        for (Origin o : {Origin::Person, Origin::Automatic, Origin::Client})
            TEST_ASSERT_TRUE(RoofIo::checkCommand(c, o, gates, b.roof, ctx()).accepted);
        // A person's and the controller's own stops pass even with a close waiting in bad weather.
        for (Origin o : {Origin::Person, Origin::Automatic})
            TEST_ASSERT_TRUE(RoofIo::checkCommand(c, o, gates, b.roof, ctx(true)).accepted);
    }
}

// Constitution III: while the weather isn't safe, a client can't stop a close,
// whoever started it (spec 005's, a person's or its own).
void test_client_cannot_stop_a_close_in_bad_weather(void)
{
    RoofIo::GateInputs unsafe = safeAndParked();
    unsafe.weather = Safety::Verdict::Unsafe;
    unsafe.weatherText = "unsafe: rain";
    for (Origin starter : {Origin::Automatic, Origin::Person, Origin::Client})
    {
        Bench b(Settings(), 1850.0f);
        TEST_ASSERT_TRUE(run(b, Command::Close, starter, unsafe));
        b.steps(50);
        TEST_ASSERT_TRUE(b.state() == State::Closing);
        for (Command c : {Command::Stop, Command::Abort})
        {
            const RoofIo::CommandCheck check = RoofIo::checkCommand(c, Origin::Client, unsafe, b.roof, ctx());
            TEST_ASSERT_FALSE(check.accepted);
            TEST_ASSERT_EQUAL_STRING("unsafeClose", check.reason.c_str());
            TEST_ASSERT_EQUAL_STRING(
                "The roof is closing and the weather isn't safe (unsafe: rain): an imaging app can't stop it (Stop in the web UI can)",
                check.text.c_str());
            TEST_ASSERT_FALSE(run(b, c, Origin::Client, unsafe));
        }
        // A person's stop and the controller's own still work.
        TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Stop, Origin::Person, unsafe, b.roof, ctx()).accepted);
        TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Stop, Origin::Automatic, unsafe, b.roof, ctx()).accepted);
        TEST_ASSERT_TRUE(b.runToRest());
        TEST_ASSERT_TRUE(b.state() == State::Closed);
    }
    // Unknown weather counts as not safe; a waiting close is covered too.
    RoofIo::GateInputs unknown = safeAndParked();
    unknown.weather = Safety::Verdict::Unknown;
    Bench open(Settings(), 1850.0f);
    TEST_ASSERT_FALSE(RoofIo::checkCommand(Command::Stop, Origin::Client, unknown, open.roof, ctx(true)).accepted);
    // Safe weather: a client may stop a close, and nothing is closing: a client may stop.
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Stop, Origin::Client, safeAndParked(), open.roof, ctx(true)).accepted);
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Stop, Origin::Client, unsafe, open.roof, ctx()).accepted);
}

// A client may stop a person's close, but then may not reopen the roof.
void test_client_cannot_reopen_a_stopped_close(void)
{
    Bench b(Settings(), 1850.0f);
    const RoofIo::GateInputs gates = safeAndParked();
    TEST_ASSERT_TRUE(run(b, Command::Close, Origin::Person, gates));
    b.steps(100);
    TEST_ASSERT_TRUE(run(b, Command::Stop, Origin::Client, gates));
    b.steps(10);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
    const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Open, Origin::Client, gates, b.roof, ctx());
    TEST_ASSERT_FALSE(check.accepted);
    TEST_ASSERT_EQUAL_STRING("closing", check.reason.c_str());
    // It may close; a person may open.
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Close, Origin::Client, gates, b.roof, ctx()).accepted);
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Open, Origin::Person, gates, b.roof, ctx()).accepted);
    // Stopped while opening: the client may carry on opening.
    Bench c;
    TEST_ASSERT_TRUE(run(c, Command::Open, Origin::Client, gates));
    c.steps(100);
    TEST_ASSERT_TRUE(run(c, Command::Stop, Origin::Client, gates));
    c.steps(10);
    TEST_ASSERT_TRUE(c.state() == State::Stopped);
    TEST_ASSERT_TRUE(RoofIo::checkCommand(Command::Open, Origin::Client, gates, c.roof, ctx()).accepted);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_shutter_numbers_are_ascom_s);
    RUN_TEST(test_shutter_state_without_a_waiting_close);
    RUN_TEST(test_shutter_state_with_a_waiting_close);
    RUN_TEST(test_nothing_but_the_closed_magnet_reads_closed);
    RUN_TEST(test_nothing_but_the_open_magnet_reads_open);
    RUN_TEST(test_a_controller_not_running_reads_error);
    RUN_TEST(test_actions_map_to_roof_commands);
    RUN_TEST(test_only_a_person_s_stop_counts);
    RUN_TEST(test_moves_never_count_as_stops);
    RUN_TEST(test_refusal_text_names_a_latched_fault);
    RUN_TEST(test_refusal_text_otherwise_unchanged);
    RUN_TEST(test_answer_carries_the_refusal);
    RUN_TEST(test_a_stop_with_the_controller_down_is_done);
    RUN_TEST(test_a_refused_stop_is_reported);
    RUN_TEST(test_every_dome_action_is_the_client_s);
    RUN_TEST(test_client_moves_refused_when_unknown);
    RUN_TEST(test_client_moves_from_a_known_position);
    RUN_TEST(test_client_cannot_clear_a_fault);
    RUN_TEST(test_client_open_refused_while_unsafe);
    RUN_TEST(test_client_close_refused_unless_parked);
    RUN_TEST(test_client_moves_when_safe_and_parked);
    RUN_TEST(test_client_unknown_position_refused_through_the_check);
    RUN_TEST(test_client_cannot_reverse_a_close);
    RUN_TEST(test_stops_pass_every_check);
    RUN_TEST(test_client_cannot_stop_a_close_in_bad_weather);
    RUN_TEST(test_client_cannot_reopen_a_stopped_close);
    return UNITY_END();
}
