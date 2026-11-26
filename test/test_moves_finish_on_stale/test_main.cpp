// Spec 047 (constitution 12.0.0, II): "a command is a command". A move that
// started with the park confirmed goes on to its end when N.I.N.A. merely stops
// answering (findings F36/F36b: the network down, N.I.N.A. unreachable, its
// reading past its freshness). It still stops on a fresh answer that says "not
// parked" (not at park, slewing, mount not connected), a park switch opening,
// a fault, Stop, and for an open the weather and daylight. The start rule is
// unchanged. The control step is RoofControl::checkGates, stepped here with
// the simulated roof and the park pipeline (rememberNina, evaluatePark,
// closeHold) as the main loop hands it over every 250 ms.

#include <functional>
#include <memory>
#include <string>
#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "FaultRecord.h"
#include "NinaApi.h"
#include "ParkRule.h"
#include "RoofAlerts.h"
#include "RoofGates.h"
#include "PollSchedule.h"
#include "RoofStatus.h"
#include "RoofText.h"
#include "StaleRide.h"

using namespace SQM;
using Nina::MountReading;
using Nina::ReadingStatus;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t GEN = 1;
    constexpr uint32_t HANDOVER_MS = 250;

    MountReading good(uint64_t at, bool atPark = true, bool slewing = false, bool connected = true)
    {
        MountReading m;
        m.status = ReadingStatus::Good;
        m.connected = connected;
        m.atPark = atPark;
        m.slewing = slewing;
        m.takenAtMs = at;
        return m;
    }

    // What the poller holds at `nowMs`: N.I.N.A. as scripted, read every 2 s.
    using Script = std::function<MountReading(uint64_t readAtMs)>;

    struct Rig
    {
        Bench bench;
        Script nina;
        Safety::NinaMemory memory;
        RoofIo::GateInputs gates;
        RoofIo::StaleRide ride;
        uint8_t fitted = 0;
        Safety::SwitchReading switch1 = Safety::SwitchReading::Closed;
        bool weatherSafe = true;
        bool daylightCloses = false;
        uint64_t lastHandOverMs = 0;
        uint64_t lastReadMs = 0;
        MountReading held;
        // What happened.
        bool interlockStopped = false;
        uint32_t interlockBits = 0;
        bool openStopped = false;
        int began = 0;
        int finished = 0;
        std::string beganText;
        std::string finishedText;

        explicit Rig(Script script, float startMm = 0.0f)
            : bench(Roof::Settings(), startMm),
              nina(std::move(script))
        {
            handOver();
        }

        uint64_t now() const { return bench.nowMs; }

        // The main loop: the poller's latest reading, judged, handed over.
        void handOver()
        {
            if (lastReadMs == 0 || now() - lastReadMs >= Safety::NINA_POLL_MS)
            {
                held = nina(now());
                lastReadMs = now();
            }
            Safety::ParkInputs in;
            in.nina = held;
            in.fittedSwitches = fitted;
            in.switch1 = switch1;
            in.nowMs = now();
            Safety::rememberNina(memory, in.nina, GEN);
            const Safety::ParkResult park = Safety::evaluatePark(in);
            const Safety::CloseHold hold = Safety::closeHold(in, park, memory);
            gates.weather = weatherSafe ? Safety::Verdict::Safe : Safety::Verdict::Unsafe;
            gates.weatherReasons = weatherSafe ? 0 : Safety::ReasonRain;
            gates.daylightCloses = daylightCloses;
            gates.parked = park.parked;
            gates.ninaGrace = hold.riding;
            gates.parkFreshUntilMs = hold.freshUntilMs;
            gates.parkUnanswered = hold.unanswered;
            gates.fittedSwitches = fitted;
            lastHandOverMs = now();
        }

        // The control task's step: RoofControl::checkGates, then the tick.
        void step()
        {
            if (now() - lastHandOverMs >= HANDOVER_MS)
                handOver();
            RoofIo::GateInputs g = gates;
            g.switch1 = switch1 == Safety::SwitchReading::Closed ? RoofIo::Contact::Closed : RoofIo::Contact::Open;
            g.switch2 = RoofIo::Contact::Closed;
            g.nowMs = now();
            const Roof::Status &status = bench.roof.status();
            const RoofIo::StaleStep s = ride.step(status, RoofIo::movesOnStale(status, g));
            if (s == RoofIo::StaleStep::Began)
            {
                ++began;
                beganText = RoofIo::staleRideText(s, ride.target());
            }
            if (s == RoofIo::StaleStep::Finished)
            {
                ++finished;
                finishedText = RoofIo::staleRideText(s, ride.target());
            }
            if (RoofIo::interlockLost(status, g))
            {
                interlockStopped = true;
                interlockBits = RoofIo::interlockRecordBits(status, g);
                bench.roof.raiseFault(
                    RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, static_cast<float>(interlockBits), 0.0f).fault);
            }
            else if (RoofIo::openMustStop(status, g))
            {
                openStopped = true;
                bench.roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            }
            bench.step();
        }

        // A command as RoofControl takes it: the gates first (the start rule).
        RoofIo::GateRefusal command(Roof::Command c, Roof::Origin origin = Roof::Origin::Automatic)
        {
            RoofIo::GateInputs g = gates;
            g.switch1 = switch1 == Safety::SwitchReading::Closed ? RoofIo::Contact::Closed : RoofIo::Contact::Open;
            g.switch2 = RoofIo::Contact::Closed;
            const RoofIo::GateRefusal refusal = RoofIo::gateCommand(c, g);
            if (refusal == RoofIo::GateRefusal::None)
                TEST_ASSERT_TRUE(bench.roof.command(c, origin).accepted);
            return refusal;
        }

        void runFor(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += bench.stepMs)
                step();
        }

        bool moving() const
        {
            const Roof::State s = bench.state();
            return s == Roof::State::Opening || s == Roof::State::Closing;
        }

        void runToRest(uint32_t maxMs = 400000)
        {
            for (uint32_t t = 0; t < maxMs && (t == 0 || moving()); t += bench.stepMs)
                step();
            step(); // the step that sees it at rest
        }
    };

    // N.I.N.A. parked until `dropMs`, then each read fails (no network).
    Script unreachableAfter(uint64_t dropMs)
    {
        return [dropMs](uint64_t at)
        {
            if (at < dropMs)
                return good(at);
            return Nina::failedReading(ReadingStatus::Unreachable, "read Timeout", at);
        };
    }

    // N.I.N.A. parked until `dropMs`; after it the poller never reads again (the
    // reading stays as it was and ages past its freshness).
    Script silentAfter(uint64_t dropMs, uint64_t sinceMs)
    {
        auto last = std::make_shared<MountReading>(good(sinceMs));
        return [dropMs, last](uint64_t at)
        {
            if (at < dropMs)
                *last = good(at);
            return *last;
        };
    }

    // N.I.N.A. parked until `atMs`, then it answers with `reading` (stamped fresh).
    Script answersAfter(uint64_t atMs, MountReading reading, uint64_t dropMs = 0)
    {
        return [=](uint64_t at)
        {
            if (dropMs != 0 && at >= dropMs && at < atMs)
                return Nina::failedReading(ReadingStatus::Unreachable, "read Timeout", at);
            if (at < atMs)
                return good(at);
            MountReading r = reading;
            r.takenAtMs = at;
            return r;
        };
    }

    Roof::Status inState(Roof::State state, Roof::End target = Roof::End::Closed)
    {
        Roof::Status s;
        s.state = state;
        s.target = target;
        return s;
    }
} // namespace

// --- The F36 timeline: the close and the open finish ---------------------------

void test_a_close_finishes_when_nina_becomes_unreachable(void)
{
    // Open first (parked), then the network drops mid-close.
    Rig open(unreachableAfter(UINT64_MAX), 0.0f);
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, open.command(Roof::Command::Open));
    open.runToRest();
    TEST_ASSERT_EQUAL(Roof::State::Open, open.bench.state());
    TEST_ASSERT_EQUAL(0, open.began);

    open.nina = unreachableAfter(open.now() + 3000); // N.I.N.A. lost 3 s into the close
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, open.command(Roof::Command::Close));
    open.runToRest();
    TEST_ASSERT_FALSE(open.interlockStopped);
    TEST_ASSERT_EQUAL(Roof::State::Closed, open.bench.state());
    TEST_ASSERT_EQUAL(1, open.began);
    TEST_ASSERT_EQUAL(1, open.finished);
    TEST_ASSERT_EQUAL_STRING(
        "Close continues without a fresh park reading (N.I.N.A. unreachable): it started with the park confirmed", open.beganText.c_str());
    TEST_ASSERT_EQUAL_STRING("Finished the close without a fresh park reading (N.I.N.A. unreachable)", open.finishedText.c_str());
}

void test_an_open_finishes_when_nina_goes_silent(void)
{
    Rig rig(silentAfter(UINT64_MAX, 0), 0.0f);
    rig.nina = silentAfter(rig.now() + 2000, rig.now()); // the poller stops reading 2 s in
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, rig.command(Roof::Command::Open, Roof::Origin::Client));
    rig.runToRest();
    TEST_ASSERT_FALSE(rig.interlockStopped);
    TEST_ASSERT_FALSE(rig.openStopped);
    TEST_ASSERT_EQUAL(Roof::State::Open, rig.bench.state());
    TEST_ASSERT_EQUAL(1, rig.finished);
    TEST_ASSERT_EQUAL_STRING("Finished the open without a fresh park reading (N.I.N.A. unreachable)", rig.finishedText.c_str());
}

void test_a_move_with_fresh_readings_throughout_reports_nothing(void)
{
    Rig rig(unreachableAfter(UINT64_MAX), 0.0f);
    rig.command(Roof::Command::Open);
    rig.runToRest();
    rig.command(Roof::Command::Close);
    rig.runToRest();
    TEST_ASSERT_EQUAL(Roof::State::Closed, rig.bench.state());
    TEST_ASSERT_EQUAL(0, rig.began);
    TEST_ASSERT_EQUAL(0, rig.finished);
}

// --- Still stops ------------------------------------------------------------------

// N.I.N.A. answers, freshly, that the mount isn't parked: stop, in either
// direction, also after it had been unreachable for a while.
void test_a_fresh_not_parked_answer_stops_either_move(void)
{
    const MountReading answers[] = {
        good(0, false),              // not at park
        good(0, true, true),         // slewing
        good(0, true, false, false), // mount not connected in N.I.N.A.
    };
    for (const MountReading &answer : answers)
    {
        for (Roof::Command c : {Roof::Command::Open, Roof::Command::Close})
        {
            Rig rig(unreachableAfter(UINT64_MAX), c == Roof::Command::Open ? 0.0f : 1850.0f);
            if (c == Roof::Command::Close)
            {
                rig.bench.roof.command(Roof::Command::Open, Roof::Origin::Person);
                rig.bench.runToRest();
            }
            const uint64_t t = rig.now();
            rig.nina = answersAfter(t + 20000, answer, t + 3000); // unreachable from 3 s, answers at 20 s
            TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, rig.command(c));
            rig.runToRest();
            TEST_ASSERT_TRUE(rig.interlockStopped);
            TEST_ASSERT_EQUAL(Roof::State::Error, rig.bench.state());
            TEST_ASSERT_EQUAL(0, rig.finished);
            TEST_ASSERT_TRUE((rig.interlockBits & RoofIo::InterlockParkResult) != 0);
            const RoofIo::FaultRecord record =
                RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, static_cast<float>(rig.interlockBits), 0.0f);
            const std::string text = RoofIo::describeFault(record);
            const std::string expected = c == Roof::Command::Open ? "Park lost during the open: park not confirmed"
                                                                  : "Park lost during the close: park not confirmed";
            TEST_ASSERT_EQUAL_STRING(expected.c_str(), text.c_str());
        }
    }
}

void test_a_park_switch_opening_stops_a_move_on_stale(void)
{
    Rig rig(unreachableAfter(UINT64_MAX), 0.0f);
    rig.fitted = 1;
    rig.handOver();
    rig.nina = unreachableAfter(rig.now() + 1000);
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, rig.command(Roof::Command::Open));
    rig.runFor(15000); // well past the freshness: on stale
    TEST_ASSERT_EQUAL(1, rig.began);
    TEST_ASSERT_EQUAL(Roof::State::Opening, rig.bench.state());
    rig.switch1 = Safety::SwitchReading::Open;
    rig.runToRest();
    TEST_ASSERT_TRUE(rig.interlockStopped);
    // In the control step that reads it, before the next hand-over.
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockSwitch1 | RoofIo::InterlockDuringOpen, rig.interlockBits);
    TEST_ASSERT_EQUAL(0, rig.finished);
}

void test_stop_and_a_fault_still_stop_a_move_on_stale(void)
{
    {
        Rig rig(unreachableAfter(UINT64_MAX), 0.0f);
        rig.nina = unreachableAfter(rig.now() + 1000);
        rig.command(Roof::Command::Open);
        rig.runFor(15000);
        TEST_ASSERT_EQUAL(1, rig.began);
        TEST_ASSERT_TRUE(rig.bench.roof.command(Roof::Command::Stop, Roof::Origin::Person).accepted);
        rig.runFor(2000);
        TEST_ASSERT_EQUAL(Roof::State::Stopped, rig.bench.state());
        TEST_ASSERT_EQUAL(0, rig.finished); // stopped short: its own stop says so
        TEST_ASSERT_FALSE(rig.ride.riding());
    }
    {
        Rig rig(unreachableAfter(UINT64_MAX), 0.0f);
        rig.nina = unreachableAfter(rig.now() + 1000);
        rig.command(Roof::Command::Open);
        rig.runFor(15000);
        Roof::Fault f;
        f.kind = Roof::FaultKind::OverCurrent;
        rig.bench.roof.raiseFault(f);
        rig.runFor(2000);
        TEST_ASSERT_EQUAL(Roof::State::Error, rig.bench.state());
        TEST_ASSERT_EQUAL(0, rig.finished);
    }
}

void test_an_open_on_stale_still_stops_for_the_weather_and_daylight(void)
{
    for (int which = 0; which < 2; ++which)
    {
        Rig rig(unreachableAfter(UINT64_MAX), 0.0f);
        rig.nina = unreachableAfter(rig.now() + 1000);
        rig.command(Roof::Command::Open);
        rig.runFor(15000);
        TEST_ASSERT_EQUAL(1, rig.began);
        if (which == 0)
            rig.weatherSafe = false; // rain
        else
            rig.daylightCloses = true;
        rig.runFor(2000);
        TEST_ASSERT_TRUE(rig.openStopped);
        TEST_ASSERT_FALSE(rig.interlockStopped);
        TEST_ASSERT_EQUAL(Roof::State::Stopped, rig.bench.state());
        TEST_ASSERT_EQUAL(0, rig.finished);
    }
}

// --- The start rule is unchanged -------------------------------------------------

void test_no_move_starts_on_a_stale_or_unanswered_park(void)
{
    Rig rig(unreachableAfter(UINT64_MAX), 0.0f);
    rig.nina = unreachableAfter(rig.now() + 1);
    rig.runFor(15000); // at rest, well past the freshness
    TEST_ASSERT_TRUE(rig.gates.parkUnanswered);
    TEST_ASSERT_FALSE(rig.gates.parked);
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::MountNotParked, rig.command(Roof::Command::Open));
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::MountNotParked, rig.command(Roof::Command::Close));
    TEST_ASSERT_FALSE(RoofIo::parkHolds(rig.gates));
    TEST_ASSERT_EQUAL(Roof::State::Closed, rig.bench.state());
    // A good reading gone stale (the poller silent) refuses a start too.
    Rig silent(silentAfter(UINT64_MAX, 0), 0.0f);
    silent.nina = silentAfter(silent.now() + 1, silent.now());
    silent.runFor(15000);
    TEST_ASSERT_TRUE(silent.gates.parkUnanswered);
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::MountNotParked, silent.command(Roof::Command::Open));
}

// --- The pure parts --------------------------------------------------------------

void test_unanswered_needs_the_last_answer_parked_and_nothing_else(void)
{
    Safety::NinaMemory memory;
    Safety::ParkInputs in;
    in.nina = good(1000, false); // the last answer: not at park
    in.nowMs = 1000;
    Safety::rememberNina(memory, in.nina, GEN);
    in.nina = Nina::failedReading(ReadingStatus::Unreachable, "x", 30000);
    in.nowMs = 30000;
    Safety::rememberNina(memory, in.nina, GEN);
    TEST_ASSERT_FALSE(Safety::closeHold(in, Safety::evaluatePark(in), memory).unanswered);
    // A stale reading that said not parked.
    Safety::ParkInputs stale;
    stale.nina = good(1000, true, true);
    stale.nowMs = 30000;
    TEST_ASSERT_FALSE(Safety::closeHold(stale, Safety::evaluatePark(stale), Safety::NinaMemory{}).unanswered);
    // Not configured or not read yet is not "unanswered".
    for (ReadingStatus s : {ReadingStatus::NotConfigured, ReadingStatus::Waiting})
    {
        Safety::ParkInputs other;
        other.nina = Nina::failedReading(s, "", 30000);
        other.nowMs = 30000;
        TEST_ASSERT_FALSE(Safety::closeHold(other, Safety::evaluatePark(other), memory).unanswered);
    }
    // With the switches alone nothing from N.I.N.A. is relied on.
    Safety::ParkInputs switches;
    switches.switchesOnly = true;
    switches.fittedSwitches = 1;
    switches.switch1 = Safety::SwitchReading::Closed;
    switches.nina = Nina::failedReading(ReadingStatus::Unreachable, "x", 30000);
    TEST_ASSERT_FALSE(Safety::closeHold(switches, Safety::evaluatePark(switches), memory).unanswered);
}

void test_overridden_and_nina_lost_moves_are_not_reported_on_stale(void)
{
    RoofIo::GateInputs g;
    g.parkUnanswered = true;
    TEST_ASSERT_TRUE(RoofIo::movesOnStale(inState(Roof::State::Closing), g));
    TEST_ASSERT_TRUE(RoofIo::movesOnStale(inState(Roof::State::Opening, Roof::End::Open), g));
    TEST_ASSERT_FALSE(RoofIo::movesOnStale(inState(Roof::State::Closing), g, true));               // a person's confirmed close
    TEST_ASSERT_FALSE(RoofIo::movesOnStale(inState(Roof::State::Closing), g, false, true));        // the N.I.N.A.-lost close
    TEST_ASSERT_FALSE(RoofIo::movesOnStale(inState(Roof::State::Opening), g, false, false, true)); // a person's confirmed open
    TEST_ASSERT_FALSE(RoofIo::movesOnStale(inState(Roof::State::Closed), g));
    g.parked = true; // fresh
    TEST_ASSERT_FALSE(RoofIo::movesOnStale(inState(Roof::State::Closing), g));
}

void test_the_stale_ride_reports_once_per_move(void)
{
    RoofIo::StaleRide ride;
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::None, ride.step(inState(Roof::State::Closing), false));
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::Began, ride.step(inState(Roof::State::Closing), true));
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::None, ride.step(inState(Roof::State::Closing), true));
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::None, ride.step(inState(Roof::State::Closing), false)); // fresh again: still reported at the end
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::Finished, ride.step(inState(Roof::State::Closed), false));
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::None, ride.step(inState(Roof::State::Closed), false));
    // Reached the other end (a calibration leg reversing): not this move's end.
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::Began, ride.step(inState(Roof::State::Opening, Roof::End::Open), true));
    TEST_ASSERT_EQUAL(RoofIo::StaleStep::None, ride.step(inState(Roof::State::Closed), false));
    TEST_ASSERT_EQUAL_STRING("", RoofIo::staleRideText(RoofIo::StaleStep::None, Roof::End::Open).c_str());
}

void test_the_finish_alert_is_information(void)
{
    const Alerts::Alert alert = CloseSeq::staleFinishAlert("Finished the close without a fresh park reading (N.I.N.A. unreachable)");
    TEST_ASSERT_TRUE(alert.type == Alerts::AlertType::FinishedUnconfirmed);
    TEST_ASSERT_TRUE(alert.level == Alerts::AlertLevel::Normal);
    TEST_ASSERT_FALSE(Alerts::isCritical(alert.type));
    TEST_ASSERT_EQUAL_STRING("Finished the close without a fresh park reading (N.I.N.A. unreachable).", alert.message.c_str());
    TEST_ASSERT_EQUAL_STRING("finished_without_park", Alerts::alertTypeName(alert.type));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_close_finishes_when_nina_becomes_unreachable);
    RUN_TEST(test_an_open_finishes_when_nina_goes_silent);
    RUN_TEST(test_a_move_with_fresh_readings_throughout_reports_nothing);
    RUN_TEST(test_a_fresh_not_parked_answer_stops_either_move);
    RUN_TEST(test_a_park_switch_opening_stops_a_move_on_stale);
    RUN_TEST(test_stop_and_a_fault_still_stop_a_move_on_stale);
    RUN_TEST(test_an_open_on_stale_still_stops_for_the_weather_and_daylight);
    RUN_TEST(test_no_move_starts_on_a_stale_or_unanswered_park);
    RUN_TEST(test_unanswered_needs_the_last_answer_parked_and_nothing_else);
    RUN_TEST(test_overridden_and_nina_lost_moves_are_not_reported_on_stale);
    RUN_TEST(test_the_stale_ride_reports_once_per_move);
    RUN_TEST(test_the_finish_alert_is_information);
    return UNITY_END();
}
