// Fix 041 (shakedown F38): a person's confirmed close (constitution II) from
// an unknown position, with N.I.N.A. unreachable, after a power cut and a
// cleared fault. The board refused it "mountNotParked": the admission checked
// it with the person's override, then RoofControl::runLocked re-checked it
// without. These are the decisions RoofControl makes, in its order, each with
// the command's overrides; the firmware's re-check now has to name them
// (RoofControl::runLocked and precheck have no default).

#include <unity.h>

#include "Config.h" // RoofIoLogic builds lib/ConfigModel; named so the native build finds it
#include "CommandCheck.h"
#include "MoveGuard.h"
#include "RoofController.h"
#include "RoofGates.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using namespace SQM::RoofIo;
using Roof::Command;
using Roof::Origin;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // N.I.N.A. down: no park result, nothing else wrong.
    GateInputs ninaDown()
    {
        GateInputs in;
        in.weather = Safety::Verdict::Safe;
        in.parked = false;
        in.parkText = "N.I.N.A. not responding: connection refused";
        return in;
    }

    MoveGuard guardWithNina()
    {
        MoveGuard guard;
        MoveGuardSettings s;
        s.ninaSet = true;
        s.requestPark = true;
        s.wait.limitMs = 120000;
        guard.configure(s);
        return guard;
    }

    CommandCheck check(const Bench &bench, Origin origin, uint8_t overrides, bool parkAside = false)
    {
        CommandContext context;
        context.overrides = overrides;
        context.parkAside = parkAside;
        return checkCommand(Command::Close, origin, ninaDown(), bench.roof, context);
    }

    Roof::CommandResult notRun()
    {
        Roof::CommandResult result;
        result.accepted = false;
        return result;
    }

    // As RoofControl::command does for a person's or a client's Close:
    // admitLocked, then commandLocked's precheck, then runLocked's re-check
    // and the controller.
    Roof::CommandResult closeAsTheFirmware(Bench &bench, MoveGuard &guard, Origin origin, uint8_t requested, std::string &reason)
    {
        const uint8_t allowed = allowedOverrides(Command::Close, origin, requested);
        const Admission admission = guard.admit(
            {Command::Close, origin, allowed, bench.roof.status()}, check(bench, origin, allowed), check(bench, origin, allowed, true), 0);
        if (admission.action != Admit::Run)
        {
            reason = admission.action == Admit::Wait ? "waiting" : admission.reason;
            return notRun();
        }
        for (int pass = 0; pass < 2; ++pass) // commandLocked's precheck, then runLocked's
        {
            const CommandCheck again = check(bench, origin, allowed);
            if (!again.accepted)
            {
                reason = again.reason;
                return notRun();
            }
        }
        const Roof::CommandResult result = bench.roof.command(Command::Close, origin);
        if (result.accepted)
            guard.ran(Command::Close, origin, allowed, ninaDown());
        return result;
    }

    // Booted mid-travel (power cut), a fault latched and cleared by a person.
    Bench unknownAfterAClearedFault()
    {
        Roof::Settings settings;
        Bench bench(settings, static_cast<float>(settings.travelMm) / 2.0f);
        bench.roof.raiseFault(Roof::Fault{Roof::FaultKind::External, 0.0f, 0.0f});
        TEST_ASSERT_TRUE(bench.roof.clearFault(Origin::Person).accepted);
        bench.steps(5);
        TEST_ASSERT_EQUAL(Roof::State::Unknown, bench.roof.status().state);
        return bench;
    }
} // namespace

void test_a_persons_confirmed_close_from_unknown_runs(void)
{
    Bench bench = unknownAfterAClearedFault();
    MoveGuard guard = guardWithNina();
    std::string reason;
    const Roof::CommandResult result = closeAsTheFirmware(bench, guard, Origin::Person, OverridePark, reason);
    TEST_ASSERT_EQUAL_STRING("", reason.c_str());
    TEST_ASSERT_TRUE(result.accepted);
    bench.steps(5);
    TEST_ASSERT_EQUAL(Roof::State::Closing, bench.roof.status().state);
    // The interlock doesn't stop it for the park the person set aside.
    TEST_ASSERT_TRUE(guard.closeParkOverride());
    TEST_ASSERT_FALSE(interlockLost(bench.roof.status(), ninaDown(), guard.closeParkOverride(), false));
}

void test_every_check_before_the_run_needs_the_override(void)
{
    // What the board answered: the same check without the override.
    Bench bench = unknownAfterAClearedFault();
    const CommandCheck dropped = check(bench, Origin::Person, 0);
    TEST_ASSERT_FALSE(dropped.accepted);
    TEST_ASSERT_EQUAL_STRING("mountNotParked", dropped.reason.c_str());
    TEST_ASSERT_TRUE(check(bench, Origin::Person, OverridePark).accepted);
}

// Spec 043 (constitution 11.1.0, II): with the position unknown a park (a
// slew) could hit the beams, so the Close doesn't wait for one: it is refused
// for the park, and only the person's confirmed close (above) goes past it.
void test_without_the_override_it_is_refused_and_no_park_is_asked(void)
{
    Bench bench = unknownAfterAClearedFault();
    MoveGuard guard = guardWithNina();
    std::string reason;
    TEST_ASSERT_FALSE(closeAsTheFirmware(bench, guard, Origin::Person, 0, reason).accepted);
    TEST_ASSERT_EQUAL_STRING("mountNotParked", reason.c_str());
    TEST_ASSERT_FALSE(guard.wait().waiting());
    TEST_ASSERT_EQUAL(Roof::State::Unknown, bench.roof.status().state);
}

void test_a_client_cant_override_the_park(void)
{
    for (Origin origin : {Origin::Client, Origin::Mqtt})
    {
        Bench bench = unknownAfterAClearedFault();
        MoveGuard guard = guardWithNina();
        std::string reason;
        TEST_ASSERT_FALSE(closeAsTheFirmware(bench, guard, origin, OverridePark, reason).accepted);
        TEST_ASSERT_EQUAL_STRING("mountNotParked", reason.c_str()); // and it couldn't move from here anyway
        TEST_ASSERT_EQUAL(Roof::State::Unknown, bench.roof.status().state);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_persons_confirmed_close_from_unknown_runs);
    RUN_TEST(test_every_check_before_the_run_needs_the_override);
    RUN_TEST(test_without_the_override_it_is_refused_and_no_park_is_asked);
    RUN_TEST(test_a_client_cant_override_the_park);
    return UNITY_END();
}
