// Roof::Controller::check(): a dry run of command() for open and close (spec
// 003 uses it so a close's side effects happen only for a close that will run).

#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

// check() answers exactly what command() would, without changing anything (spec 003 uses it
// to act on a close only when it will run).
namespace
{
    void assertCheckMatches(Bench &b, Command c, Origin o)
    {
        const Status before = b.roof.status();
        const CommandResult predicted = b.roof.check(c, o);
        TEST_ASSERT_EQUAL(static_cast<int>(before.state), static_cast<int>(b.roof.status().state)); // no change
        const CommandResult actual = b.roof.command(c, o);
        TEST_ASSERT_EQUAL(actual.accepted, predicted.accepted);
        TEST_ASSERT_EQUAL(static_cast<int>(actual.reason), static_cast<int>(predicted.reason));
    }
} // namespace

void test_check_predicts_command(void)
{
    {
        Bench b; // closed: close is a no-op, open is accepted
        assertCheckMatches(b, Command::Close, Origin::Automatic);
        assertCheckMatches(b, Command::Open, Origin::Automatic);
        assertCheckMatches(b, Command::Open, Origin::Automatic); // already on its way
    }
    {
        Bench b(Settings(), 900.0f); // unknown position
        assertCheckMatches(b, Command::Close, Origin::Automatic);
        assertCheckMatches(b, Command::Close, Origin::Person);
    }
    {
        Bench b;
        b.roof.raiseFault(Fault{FaultKind::External, 1.0f, 0.0f});
        assertCheckMatches(b, Command::Close, Origin::Person);
        assertCheckMatches(b, Command::Open, Origin::Person);
    }
    {
        Bench b(Settings(), 1850.0f);
        b.sim.supplyVolts = 9.0f;
        b.steps(2);
        assertCheckMatches(b, Command::Close, Origin::Person);
    }
    {
        Controller fresh; // Hall not settled yet
        TEST_ASSERT_EQUAL(static_cast<int>(Refusal::HallNotSettled), static_cast<int>(fresh.check(Command::Close, Origin::Person).reason));
        TEST_ASSERT_TRUE(fresh.check(Command::Stop, Origin::Automatic).accepted);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_predicts_command);
    return UNITY_END();
}
