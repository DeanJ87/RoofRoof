// An Alpaca client against spec 005's close on unsafe weather (spec 006 FR-007,
// constitution III): the shared command check, the real motion logic and the
// real close sequence, with RoofControl's command flow (src/RoofControl.cpp:
// stopLocked, commandLocked) reduced to its decisions. Whatever a client sends,
// in any order, the roof still ends closed.

#include <unity.h>

#include "CloseSequence.h"
#include "CommandCheck.h"
#include "Config.h"
#include "DomeBridge.h"
#include "RoofController.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using Roof::Command;
using Roof::Origin;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // RoofControl's flow: the check first, a stop clears a waiting close and
    // counts if it is a person's, a close may be deferred behind a blocker.
    struct ControlledRoof
    {
        RoofTest::Bench bench;
        RoofIo::GateInputs gates;
        bool closePending = false;
        bool blocked = false; // an upload or MQTT connect: closes wait
        Origin closeOrigin = Origin::Person;
        uint32_t personStops = 0;

        explicit ControlledRoof(float startMm)
            : bench(SQM::Roof::Settings(), startMm)
        {
            gates.weather = Safety::Verdict::Unsafe;
            gates.weatherText = "unsafe: rain";
            gates.parked = true;
            gates.parkText = "parked";
        }

        bool command(Command c, Origin o)
        {
            RoofIo::CommandContext context;
            context.closePending = closePending;
            if (!RoofIo::checkCommand(c, o, gates, bench.roof, context).accepted)
                return false;
            if (c == Command::Stop || c == Command::Abort)
            {
                closePending = false;
                if (RoofIo::countsAsPersonStop(c, o))
                    personStops++;
                return bench.command(c, o).accepted;
            }
            const State s = bench.state();
            if (c == Command::Close && blocked && s != State::Closed && s != State::Closing)
            {
                closePending = true;
                closeOrigin = o;
                return true;
            }
            const bool ok = bench.command(c, o).accepted;
            if (c == Command::Open && ok)
                closePending = false;
            return ok;
        }

        // runPendingClose once the blocker has gone.
        void unblock()
        {
            blocked = false;
            if (closePending)
            {
                closePending = false;
                command(Command::Close, closeOrigin);
            }
        }
    };

    // Steps roof and sequence for `ms`, the client acting through `client(nowMs)`.
    template <typename Client> void runFor(ControlledRoof &roof, CloseSeq::Sequence &seq, uint64_t &now, uint64_t ms, Client client)
    {
        CloseSeq::Inputs in;
        in.weatherSafe = false;
        in.parked = true;
        in.ninaConfigured = true;
        for (uint64_t end = now + ms; now < end; now += 100)
        {
            roof.bench.steps(5);
            client(now);
            in.nowMs = now;
            in.roof = roof.bench.state();
            in.closePending = roof.closePending;
            in.personStops = roof.personStops;
            in.moveTimeoutMs = roof.bench.roof.status().moveTimeoutMs;
            const CloseSeq::Decision d = seq.step(in);
            if (d.action == CloseSeq::Action::Close)
                seq.closeAnswered(
                    roof.command(Command::Close, Origin::Automatic) ? CloseSeq::CloseAnswer::Started : CloseSeq::CloseAnswer::Refused);
            else if (d.action == CloseSeq::Action::Stop)
                roof.command(Command::Stop, Origin::Automatic);
        }
    }
} // namespace

// A client that keeps closing and stopping (each stop a moment after its close)
// can't hold the roof open: its stops are refused, the close runs to the magnet.
void test_close_then_stop_loop_still_ends_closed(void)
{
    ControlledRoof roof(1850.0f);
    CloseSeq::Sequence seq;
    uint64_t now = 0;
    int stopsAccepted = 0;
    runFor(
        roof,
        seq,
        now,
        3600ull * 1000,
        [&](uint64_t t)
        {
            if (t % 20000 == 1000)
                roof.command(Command::Close, Origin::Client);
            if (t % 20000 == 1300 || t % 7000 == 0)
            {
                const bool closing = roof.bench.state() == State::Closing || roof.closePending;
                const bool accepted = roof.command(Command::Stop, Origin::Client);
                TEST_ASSERT_FALSE(accepted && closing); // only a stop with nothing closing gets through
                stopsAccepted += accepted ? 1 : 0;
            }
        });
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed);
    // Only stops while nothing was closing got through, and none counted as a person's.
    TEST_ASSERT_EQUAL_UINT32(0, roof.personStops);
    TEST_ASSERT_TRUE(stopsAccepted > 0); // the client was active throughout
}

// A client's close while spec 005's close waits behind an upload: the client
// can't then cancel the waiting close by stopping it.
void test_client_cannot_cancel_a_waiting_close(void)
{
    ControlledRoof roof(1850.0f);
    roof.blocked = true;
    TEST_ASSERT_TRUE(roof.command(Command::Close, Origin::Automatic));
    TEST_ASSERT_TRUE(roof.closePending);
    TEST_ASSERT_FALSE(roof.command(Command::Stop, Origin::Client));
    TEST_ASSERT_TRUE(roof.command(Command::Close, Origin::Client)); // re-deferred as the client's
    TEST_ASSERT_FALSE(roof.command(Command::Abort, Origin::Client));
    TEST_ASSERT_TRUE(roof.closePending);
    roof.unblock();
    TEST_ASSERT_TRUE(roof.bench.runToRest());
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed);
}

// The sequence's own stop and a person's are never refused.
void test_person_and_controller_stops_still_work(void)
{
    ControlledRoof roof(1850.0f);
    TEST_ASSERT_TRUE(roof.command(Command::Close, Origin::Automatic));
    roof.bench.steps(50);
    TEST_ASSERT_TRUE(roof.command(Command::Stop, Origin::Automatic));
    TEST_ASSERT_TRUE(roof.command(Command::Close, Origin::Automatic));
    roof.bench.steps(50);
    TEST_ASSERT_TRUE(roof.command(Command::Stop, Origin::Person));
    TEST_ASSERT_EQUAL_UINT32(1, roof.personStops);
    roof.bench.steps(10);
    TEST_ASSERT_TRUE(roof.bench.state() == State::Stopped);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_close_then_stop_loop_still_ends_closed);
    RUN_TEST(test_client_cannot_cancel_a_waiting_close);
    RUN_TEST(test_person_and_controller_stops_still_work);
    return UNITY_END();
}
