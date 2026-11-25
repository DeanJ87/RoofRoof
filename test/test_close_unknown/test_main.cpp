// Spec 030 (constitution 8.0.0): the automatic close with the sequence and the
// roof controller together. W05: a crash 10 s into an open, a restart with the
// Hall healthy between the magnets, unsafe weather: once the park is confirmed
// the roof closes at the slow duty to the closed magnet, and never before. The
// blind close with the Hall sensor dead, once. The N.I.N.A.-lost close
// (autoClose.ninaLostClose): only the automatic close, only after the time,
// the switches still counting.

#include <unity.h>
#include <vector>

#include "../close_rig/CloseRig.h"
#include "../roof_sim/RoofSim.h"
#include "ClosePolicy.h"
#include "CommandCheck.h"
#include "Config.h"
#include "MotionProfile.h"
#include "MoveGuard.h"
#include "NinaLost.h"
#include "RoofAlerts.h"
#include "RoofController.h"
#include "RoofGates.h"

using namespace SQM;
using namespace SQM::CloseSeq;
using Roof::State;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const uint8_t SLOW_DUTY = Roof::toDuty(0.40f);

    // The sequence stepped every 100 ms over a controller and its simulated
    // roof stepped every 20 ms, the gates between them as RoofControl has them.
    struct Site
    {
        Bench &b;
        Sequence seq;
        bool weatherSafe = true;
        bool parked = false;
        bool ninaLost = false;
        RoofIo::GateInputs gates;
        bool ninaLostClose = false; // the close under way set N.I.N.A.'s park aside
        std::vector<Decision> events;
        int closes = 0;
        int blindCloses = 0;
        uint8_t maxDuty = 0;
        float startMm = 0.0f;
        bool movedBeforePark = false;

        explicit Site(Bench &bench)
            : b(bench)
        {
        }

        void note(const Decision &d)
        {
            if (d.event != Event::None)
                events.push_back(d);
        }

        void carryOut(const Decision &d)
        {
            if (d.action == Action::Stop)
                b.roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            if (d.action != Action::Close && d.action != Action::BlindClose)
                return;
            RoofIo::CommandContext context;
            context.overrides = d.ninaLost ? RoofIo::OverrideNinaLost : 0;
            bool accepted = false;
            std::string reason;
            if (d.action == Action::Close)
            {
                ++closes;
                const RoofIo::CommandCheck check =
                    RoofIo::checkCommand(Roof::Command::Close, Roof::Origin::Automatic, gates, b.roof, context);
                accepted = check.accepted && b.roof.command(Roof::Command::Close, Roof::Origin::Automatic).accepted;
                reason = check.reason;
            }
            else
            {
                ++blindCloses;
                const uint8_t bits = RoofIo::allowedOverrides(Roof::Command::Close, Roof::Origin::Automatic, context.overrides);
                const RoofIo::GateRefusal gate = RoofIo::gateCommand(Roof::Command::Close, gates, bits);
                accepted = gate == RoofIo::GateRefusal::None && b.roof.blindClose(Roof::Origin::Automatic).accepted;
                reason = RoofIo::gateRefusalName(gate);
            }
            if (accepted)
                ninaLostClose = d.ninaLost;
            note(seq.closeAnswered(answerFor(accepted, reason)));
        }

        void step100()
        {
            gates.weather = weatherSafe ? Safety::Verdict::Safe : Safety::Verdict::Unsafe;
            gates.parked = parked;
            for (int i = 0; i < 5; ++i)
            {
                const Roof::Output out = b.step();
                if (out.duty > maxDuty)
                    maxDuty = out.duty;
                if (RoofIo::interlockLost(b.roof.status(), gates, false, ninaLostClose))
                    b.roof.raiseFault(Roof::Fault{Roof::FaultKind::External, 1.0f, 0.0f});
            }
            if (!parked && !ninaLost && b.sim.positionMm != startMm)
                movedBeforePark = true;
            Inputs in;
            in.nowMs = b.nowMs;
            in.weatherSafe = weatherSafe;
            applyRoof(in, b.roof.status(), false);
            in.parked = parked;
            in.ninaLost = ninaLost;
            const Decision d = seq.step(in);
            note(d);
            carryOut(d);
        }

        void runFor(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += 100)
                step100();
        }

        int count(Event event, Cause cause) const
        {
            int n = 0;
            for (const Decision &d : events)
                n += d.event == event && d.cause == cause && !d.reminder ? 1 : 0;
            return n;
        }
    };

    // The roof part-open at `mm` after a restart, its Hall reading between the magnets.
    float crashTenSecondsIntoAnOpen()
    {
        Bench before;
        TEST_ASSERT_TRUE(before.command(Roof::Command::Open).accepted);
        before.steps(500); // 10 s
        TEST_ASSERT_TRUE(before.state() == State::Opening);
        return before.sim.positionMm;
    }
} // namespace

// --- W05: a crash mid-open, a restart, unsafe weather ---

void test_w05_closes_slowly_to_the_closed_magnet_once_the_park_is_confirmed(void)
{
    const float mm = crashTenSecondsIntoAnOpen();
    TEST_ASSERT_TRUE(mm > 100.0f && mm < 600.0f);
    Bench b(Roof::Settings(), mm); // the restart: a fresh controller, nothing known
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_TRUE(b.roof.status().hall == Roof::HallLevel::Between);
    Site site(b);
    site.startMm = mm;
    site.runFor(5000);
    site.weatherSafe = false;
    site.runFor(120000); // N.I.N.A. hasn't parked yet: nothing moves
    TEST_ASSERT_FALSE(site.movedBeforePark);
    TEST_ASSERT_EQUAL(0, site.closes);
    TEST_ASSERT_EQUAL(Phase::WaitPark, site.seq.phase());
    TEST_ASSERT_TRUE(b.roof.output().command == Driver::Command::Off);
    site.parked = true;
    site.runFor(PARK_SETTLE_MS + 300);
    TEST_ASSERT_EQUAL(1, site.closes);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_TRUE(b.roof.status().fromUnknown);
    TEST_ASSERT_EQUAL_UINT32(Roof::unknownMoveTimeoutMs(Roof::Settings()), b.roof.status().moveTimeoutMs);
    site.runFor(120000);
    TEST_ASSERT_TRUE(b.state() == State::Closed);
    TEST_ASSERT_TRUE(b.sim.atClosedMagnet());
    TEST_ASSERT_TRUE(site.maxDuty <= SLOW_DUTY);
    TEST_ASSERT_EQUAL(1, site.closes);
    // Closed, then the critical "position had been unknown" event.
    TEST_ASSERT_EQUAL(1, site.count(Event::Closed, Cause::None));
    TEST_ASSERT_EQUAL(1, site.count(Event::NeedsPerson, Cause::WasUnknown));
    for (const Decision &d : site.events)
        if (d.event == Event::NeedsPerson)
        {
            TEST_ASSERT_TRUE(d.alert);
            Alerts::AlertType type = Alerts::AlertType::RoofNotClosed;
            TEST_ASSERT_TRUE(alertTypeFor(d.event, type));
            TEST_ASSERT_TRUE(type == Alerts::AlertType::NeedsPerson);
            TEST_ASSERT_EQUAL_STRING("Roof closed from an unknown position", alertFor(d, "").title.c_str());
        }
    TEST_ASSERT_EQUAL(Phase::Idle, site.seq.phase());
}

void test_w05_without_the_park_never_moves(void)
{
    const float mm = crashTenSecondsIntoAnOpen();
    Bench b(Roof::Settings(), mm);
    Site site(b);
    site.startMm = mm;
    site.weatherSafe = false;
    site.runFor(3600000); // an hour of park requests and alerts
    TEST_ASSERT_FALSE(site.movedBeforePark);
    TEST_ASSERT_EQUAL(0, site.closes);
    TEST_ASSERT_TRUE(b.state() == State::Unknown);
    TEST_ASSERT_TRUE(site.count(Event::ParkNotConfirmed, Cause::ParkTimeout) >= 1);
}

void test_the_unknown_close_ending_on_the_wrong_magnet_faults_and_still_says_so(void)
{
    Bench b(Roof::Settings(), 900.0f);
    b.sim.reversedWiring = true; // "close" drives towards open
    Site site(b);
    site.weatherSafe = false;
    site.parked = true;
    site.runFor(200000);
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == Roof::FaultKind::WrongMagnet);
    TEST_ASSERT_EQUAL(1, site.closes);
    TEST_ASSERT_EQUAL(1, site.count(Event::CloseFailed, Cause::Fault));
    TEST_ASSERT_EQUAL(1, site.count(Event::NeedsPerson, Cause::WasUnknown));
    TEST_ASSERT_EQUAL(Phase::CannotMove, site.seq.phase());
}

// --- The blind close (the Hall sensor dead) ---

namespace
{
    void killHall(Bench &b)
    {
        b.sim.railCounts = 4095;
        b.steps(10);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == Roof::FaultKind::HallRail);
    }
} // namespace

void test_blind_close_runs_once_after_the_park_and_asks_for_a_person(void)
{
    Bench b(Roof::Settings(), 900.0f);
    killHall(b);
    Site site(b);
    site.startMm = 900.0f;
    site.weatherSafe = false;
    site.runFor(60000);
    TEST_ASSERT_FALSE(site.movedBeforePark); // the park first
    TEST_ASSERT_EQUAL(Phase::WaitPark, site.seq.phase());
    site.parked = true;
    site.runFor(PARK_SETTLE_MS + 300);
    TEST_ASSERT_EQUAL(1, site.blindCloses);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    TEST_ASSERT_TRUE(b.roof.status().blind);
    site.runFor(Roof::unknownMoveTimeoutMs(Roof::Settings()) + 5000);
    TEST_ASSERT_TRUE(b.state() == State::Error);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.sim.positionMm);
    TEST_ASSERT_TRUE(site.maxDuty <= SLOW_DUTY);
    TEST_ASSERT_EQUAL(1, site.count(Event::NeedsPerson, Cause::ClosedBlind));
    TEST_ASSERT_EQUAL(Phase::CannotMove, site.seq.phase());
    TEST_ASSERT_EQUAL_STRING("Roof closed blind: check the roof", alertFor(site.events.back(), "").title.c_str());
    // Never a second one; the reminder repeats.
    site.events.clear();
    site.runFor(2 * 3600000);
    TEST_ASSERT_EQUAL(1, site.blindCloses);
    TEST_ASSERT_EQUAL(0, site.closes);
    TEST_ASSERT_TRUE(site.events.size() >= 2);
    for (const Decision &d : site.events)
        TEST_ASSERT_TRUE(d.reminder && d.event == Event::NeedsPerson);
}

void test_blind_close_off_leaves_the_roof_and_says_it_cant_close(void)
{
    Roof::Settings s;
    s.blindClose = false;
    Bench b(s, 900.0f);
    killHall(b);
    Site site(b);
    site.startMm = 900.0f;
    site.weatherSafe = false;
    site.parked = true;
    site.runFor(3600000);
    TEST_ASSERT_EQUAL(0, site.blindCloses + site.closes);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, b.sim.positionMm);
    TEST_ASSERT_EQUAL(1, site.count(Event::CannotClose, Cause::FaultLatched));
}

void test_any_other_fault_leaves_the_roof(void)
{
    Bench b(Roof::Settings(), 900.0f);
    b.roof.raiseFault(Roof::Fault{Roof::FaultKind::Timeout, 1.0f, 2.0f});
    Site site(b);
    site.weatherSafe = false;
    site.parked = true;
    site.runFor(600000);
    TEST_ASSERT_EQUAL(0, site.blindCloses + site.closes);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, b.sim.positionMm);
}

// The Hall sensor dies during a close: that close fails (a fault); the
// blind close may then carry on towards closed with the park still confirmed.
void test_a_hall_rail_during_a_close_then_closes_blind(void)
{
    Bench b(Roof::Settings(), 1850.0f); // open
    Site site(b);
    site.weatherSafe = false;
    site.parked = true;
    site.runFor(PARK_SETTLE_MS + 2000);
    TEST_ASSERT_EQUAL(1, site.closes);
    TEST_ASSERT_TRUE(b.state() == State::Closing);
    site.runFor(10000);
    b.sim.railCounts = 0;
    site.runFor(1000);
    TEST_ASSERT_EQUAL(1, site.count(Event::CloseFailed, Cause::Fault));
    site.runFor(2000);
    TEST_ASSERT_EQUAL(1, site.blindCloses);
    TEST_ASSERT_TRUE(b.roof.status().blind);
    site.runFor(Roof::unknownMoveTimeoutMs(Roof::Settings()) + 5000);
    TEST_ASSERT_EQUAL(1, site.blindCloses);
    TEST_ASSERT_EQUAL(1, site.count(Event::NeedsPerson, Cause::ClosedBlind));
}

// --- The N.I.N.A.-lost close (autoClose.ninaLostClose) ---

void test_nina_lost_off_never_closes_and_keeps_attempting(void)
{
    CloseTest::Rig r;
    r.in.weatherSafe = false;
    r.in.parked = false;
    CloseTest::Counts c = r.runTo(CloseTest::T0 + 2 * 3600000ULL, 1000);
    TEST_ASSERT_EQUAL(0, c.closes);
    TEST_ASSERT_TRUE(c.requests > 10);
    int parkAlerts = 0;
    for (const Decision &d : c.events)
        parkAlerts += d.event == Event::ParkNotConfirmed ? 1 : 0;
    TEST_ASSERT_TRUE(parkAlerts >= 2);
}

void test_nina_lost_on_closes_after_the_time_and_says_so(void)
{
    CloseTest::Rig r;
    r.in.weatherSafe = false;
    r.at(CloseTest::T0);
    CloseTest::Counts c = r.runTo(CloseTest::T0 + 299000, 1000);
    TEST_ASSERT_EQUAL(0, c.closes);
    r.in.ninaLost = true; // the watch's time has passed
    const Decision d = r.at(CloseTest::T0 + 300000);
    TEST_ASSERT_EQUAL(Action::Close, d.action);
    TEST_ASSERT_TRUE(d.ninaLost);
    r.seq.closeAnswered(CloseAnswer::Started);
    r.in.roof = State::Closing;
    r.in.moveTimeoutMs = 60000;
    r.at(CloseTest::T0 + 301000);
    r.in.roof = State::Closed;
    CloseTest::assertEvent(r.at(CloseTest::T0 + 340000), Event::Closed, Cause::None);
    const Decision notice = r.at(CloseTest::T0 + 340100);
    CloseTest::assertEvent(notice, Event::NeedsPerson, Cause::NinaLost);
    TEST_ASSERT_TRUE(notice.alert && notice.ninaLost);
    TEST_ASSERT_EQUAL_STRING("Roof closed without N.I.N.A.'s park", alertFor(notice, "").title.c_str());
}

void test_nina_lost_never_for_daylight_alone(void)
{
    CloseTest::Rig r;
    r.in.weatherSafe = true;
    r.in.daylight = true;
    r.in.ninaLost = true;
    CloseTest::Counts c = r.runTo(CloseTest::T0 + 3600000, 1000);
    TEST_ASSERT_TRUE(c.requests >= 1); // the daylight close runs...
    TEST_ASSERT_EQUAL(0, c.closes);    // ...but only with the park
}

void test_nina_lost_watch_needs_the_time_without_a_break(void)
{
    NinaLostWatch w;
    NinaLostWatch::Step s;
    s.enabled = true;
    s.afterMs = 300000;
    s.parkReasons = Safety::ParkNinaUnreachable;
    for (uint64_t t = 1000; t < 1000 + 300000; t += 1000)
    {
        s.nowMs = t;
        TEST_ASSERT_FALSE(w.update(s));
    }
    s.nowMs = 301000;
    TEST_ASSERT_TRUE(w.update(s));
    s.parkReasons = Safety::ParkMountNotAtPark; // N.I.N.A. answers: not lost
    s.nowMs = 302000;
    TEST_ASSERT_FALSE(w.update(s));
    for (uint32_t lostBit : {Safety::ParkNinaStale, Safety::ParkNinaUnreadable, Safety::ParkNinaApiError})
    {
        s.parkReasons = lostBit;
        s.nowMs += 1000;
        TEST_ASSERT_FALSE(w.update(s)); // the time starts again
    }
    s.nowMs += 300000;
    TEST_ASSERT_TRUE(w.update(s));
    s.enabled = false;
    TEST_ASSERT_FALSE(w.update(s));
    TEST_ASSERT_FALSE(ninaLost(Safety::ParkNinaWaiting | Safety::ParkNinaNotConfigured));
}

void test_nina_lost_follows_the_setting_and_the_park_source(void)
{
    Config config;
    TEST_ASSERT_FALSE(policyFrom(config).ninaLostClose); // off by default
    config.autoClose.ninaLostClose = true;
    config.autoClose.ninaLostMinutes = 7;
    TEST_ASSERT_TRUE(policyFrom(config).ninaLostClose);
    TEST_ASSERT_EQUAL_UINT64(420000, policyFrom(config).ninaLostMs);
    config.park.source = ParkConfig::Source::NinaAndSwitches;
    TEST_ASSERT_TRUE(policyFrom(config).ninaLostClose);
    config.park.source = ParkConfig::Source::Switches; // the switches confirm the park: N.I.N.A. doesn't matter
    TEST_ASSERT_FALSE(policyFrom(config).ninaLostClose);
}

void test_the_nina_lost_bit_is_the_automatic_closes_alone(void)
{
    using namespace RoofIo;
    for (Roof::Origin o : {Roof::Origin::Person, Roof::Origin::Client, Roof::Origin::Mqtt})
        for (Roof::Command c : {Roof::Command::Open, Roof::Command::Close})
            TEST_ASSERT_EQUAL_UINT8(0, allowedOverrides(c, o, OverrideNinaLost));
    TEST_ASSERT_EQUAL_UINT8(0, allowedOverrides(Roof::Command::Open, Roof::Origin::Automatic, OverrideNinaLost));
    TEST_ASSERT_EQUAL_UINT8(
        OverrideNinaLost, allowedOverrides(Roof::Command::Close, Roof::Origin::Automatic, OverrideNinaLost | OverridePark));
    // The whole check: a roof open at its magnet, N.I.N.A. lost (not parked).
    Bench b(Roof::Settings(), 1850.0f);
    GateInputs gates;
    gates.weather = Safety::Verdict::Unsafe;
    gates.parked = false;
    CommandContext context;
    context.overrides = OverrideNinaLost;
    for (Roof::Origin o : {Roof::Origin::Person, Roof::Origin::Client, Roof::Origin::Mqtt})
    {
        const CommandCheck check = checkCommand(Roof::Command::Close, o, gates, b.roof, context);
        TEST_ASSERT_FALSE(check.accepted);
        TEST_ASSERT_EQUAL_STRING("mountNotParked", check.reason.c_str());
    }
    TEST_ASSERT_TRUE(checkCommand(Roof::Command::Close, Roof::Origin::Automatic, gates, b.roof, context).accepted);
    TEST_ASSERT_FALSE(checkCommand(Roof::Command::Close, Roof::Origin::Automatic, gates, b.roof, CommandContext{}).accepted);
    // Never an Open, whoever asks.
    gates.weather = Safety::Verdict::Safe;
    TEST_ASSERT_TRUE(gateCommand(Roof::Command::Open, gates, OverrideNinaLost) == GateRefusal::MountNotParked);
}

void test_nina_lost_with_switches_needs_every_switch_closed_throughout(void)
{
    using namespace RoofIo;
    GateInputs gates;
    gates.parked = false;
    gates.fittedSwitches = 2;
    gates.switch1 = Contact::Closed;
    gates.switch2 = Contact::Closed;
    TEST_ASSERT_TRUE(gateCommand(Roof::Command::Close, gates, OverrideNinaLost) == GateRefusal::None);
    Roof::Status closing;
    closing.state = State::Closing;
    TEST_ASSERT_FALSE(interlockLost(closing, gates, false, true));
    TEST_ASSERT_TRUE(interlockLost(closing, gates, false, false)); // without the bit: N.I.N.A.'s park counts
    gates.switch2 = Contact::Open;
    TEST_ASSERT_TRUE(gateCommand(Roof::Command::Close, gates, OverrideNinaLost) == GateRefusal::MountNotParked);
    TEST_ASSERT_TRUE(interlockLost(closing, gates, false, true));
    gates.switch2 = Contact::Unreadable;
    TEST_ASSERT_TRUE(interlockLost(closing, gates, false, true));
    // Kept through the close by the guard, as a person's park override is.
    MoveGuard guard;
    guard.ran(Roof::Command::Close, OverrideNinaLost, gates);
    TEST_ASSERT_TRUE(guard.closeNinaLost());
    TEST_ASSERT_FALSE(guard.closeParkOverride());
    guard.ran(Roof::Command::Open, 0, gates);
    TEST_ASSERT_FALSE(guard.closeNinaLost());
}

void test_nina_lost_closes_a_dead_hall_roof_blind_too(void)
{
    Bench b(Roof::Settings(), 900.0f);
    killHall(b);
    Site site(b);
    site.weatherSafe = false;
    site.ninaLost = true; // N.I.N.A. lost long enough; no park
    site.runFor(5000);
    TEST_ASSERT_EQUAL(1, site.blindCloses);
    TEST_ASSERT_TRUE(b.roof.status().blind);
    site.runFor(Roof::unknownMoveTimeoutMs(Roof::Settings()) + 5000);
    TEST_ASSERT_EQUAL(1, site.count(Event::NeedsPerson, Cause::ClosedBlind));
    for (const Decision &d : site.events)
        if (d.event == Event::NeedsPerson)
            TEST_ASSERT_TRUE(d.ninaLost);
}

// Spec 033: a person's jog is never taken for a close to watch: the sequence
// sees the roof as the jog leaves it (unknown, or still in error).
void test_a_persons_jog_is_not_a_close_to_watch(void)
{
    Roof::Status jog;
    jog.state = State::Closing;
    jog.jogging = true;
    Inputs in;
    applyRoof(in, jog, false);
    TEST_ASSERT_TRUE(in.roof == State::Unknown);
    jog.fault.kind = Roof::FaultKind::HallRail;
    applyRoof(in, jog, false);
    TEST_ASSERT_TRUE(in.roof == State::Error);
    jog.jogging = false;
    applyRoof(in, jog, false);
    TEST_ASSERT_TRUE(in.roof == State::Closing);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_w05_closes_slowly_to_the_closed_magnet_once_the_park_is_confirmed);
    RUN_TEST(test_w05_without_the_park_never_moves);
    RUN_TEST(test_the_unknown_close_ending_on_the_wrong_magnet_faults_and_still_says_so);
    RUN_TEST(test_blind_close_runs_once_after_the_park_and_asks_for_a_person);
    RUN_TEST(test_blind_close_off_leaves_the_roof_and_says_it_cant_close);
    RUN_TEST(test_any_other_fault_leaves_the_roof);
    RUN_TEST(test_a_hall_rail_during_a_close_then_closes_blind);
    RUN_TEST(test_nina_lost_off_never_closes_and_keeps_attempting);
    RUN_TEST(test_nina_lost_on_closes_after_the_time_and_says_so);
    RUN_TEST(test_nina_lost_never_for_daylight_alone);
    RUN_TEST(test_nina_lost_watch_needs_the_time_without_a_break);
    RUN_TEST(test_nina_lost_follows_the_setting_and_the_park_source);
    RUN_TEST(test_the_nina_lost_bit_is_the_automatic_closes_alone);
    RUN_TEST(test_nina_lost_with_switches_needs_every_switch_closed_throughout);
    RUN_TEST(test_nina_lost_closes_a_dead_hall_roof_blind_too);
    RUN_TEST(test_a_persons_jog_is_not_a_close_to_watch);
    return UNITY_END();
}
