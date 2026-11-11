// The motion logic's safety properties under random use (spec 002 SC-001;
// constitution I). Thousands of seeded sequences of random settings, commands,
// origins, step timing, Hall noise and glitches, supply dips, current spikes,
// jams, wrong wiring and missing magnets drive a simulated roof. After every
// step and command an independent observer checks:
//
//   P1  never both directions; Off for the dead time between driven periods,
//       and after start-up, timed from steps (commands carry no time);
//       the direction is the one the target needs;
//   P2  the duty never exceeds the top duty, nor the slow duty from unknown;
//   P3  a latched fault keeps the driver off until a person clears it;
//   P4  no motion after the position became unknown without a person's command;
//   P5  no driven period outlasts its timeout or (from a magnet) the leave time;
//   P7  three rail readings among the last eight, consecutive or not, latch a
//       fault on that step;
//   P8  the driving toward each end since a magnet was last read (or a person
//       cleared a fault) never reaches the drive budget, and a free roof
//       reversed once part-way never trips it (P8b);
//   P6  the target magnet ends the move, at that end, on the step it is
//       confirmed.
//
// A failure names the seed and the step, so it can be replayed.

#include <cmath>
#include <cstdio>
#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "DriverLogic.h"
#include "MotionProfile.h"
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;
using RoofTest::RoofSim;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t SEQUENCES = 10000;
    constexpr int STEPS_PER_SEQUENCE = 4000; // 80 s at 20 ms: long enough for timeouts

    // xorshift32: deterministic and the same on every platform.
    struct Random
    {
        uint32_t state;
        explicit Random(uint32_t seed)
            : state(seed * 2654435761u + 1u)
        {
        }
        uint32_t next()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }
        uint32_t between(uint32_t lo, uint32_t hi) { return lo + below(hi - lo + 1); }
        bool chance(uint32_t perTenThousand) { return below(10000) < perTenThousand; }
        float uniform(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(below(1000001)) / 1000000.0f; }
    };

    Settings randomSettings(Random &rng)
    {
        Settings s;
        s.travelMm = rng.between(300, 2500);
        s.speedMmPerSecond = rng.uniform(25.0f, 150.0f);
        s.timeoutMarginMs = rng.between(3, 20) * 1000;
        s.leaveMagnetMs = rng.between(2, 12) * 1000;
        s.rampUpMs = rng.between(0, 5000);
        s.rampDownMs = rng.between(0, 6000);
        s.topDutyPercent = static_cast<uint8_t>(rng.between(20, 100));
        s.slowDutyPercent = static_cast<uint8_t>(rng.between(10, s.topDutyPercent));
        s.openDirection = rng.chance(5000) ? Driver::Command::Forward : Driver::Command::Reverse;
        s.hallHighEnd = rng.chance(5000) ? End::Open : End::Closed;
        s.hallHighCounts = static_cast<uint16_t>(rng.between(2500, 3500));
        s.hallLowCounts = static_cast<uint16_t>(rng.between(700, 1500));
        s.currentSenseEnabled = rng.chance(5000);
        s.maxCurrentAmps = rng.uniform(2.0f, 20.0f);
        s.minSupplyVolts = rng.uniform(9.0f, 12.0f);
        return s;
    }

    // The simulated roof as wired for `s`, sometimes with a fault built in.
    RoofSim simFor(const Settings &s, Random &rng)
    {
        RoofSim sim;
        sim.travelMm = static_cast<float>(s.travelMm);
        sim.speedMmPerSecond = s.speedMmPerSecond * rng.uniform(0.85f, 1.15f);
        sim.highEndClosed = s.hallHighEnd == End::Closed;
        sim.highMagnetCounts = static_cast<uint16_t>(s.hallHighCounts + 300);
        sim.lowMagnetCounts = static_cast<uint16_t>(s.hallLowCounts - 300);
        sim.reversedWiring = (s.openDirection == Driver::Command::Reverse) != rng.chance(500);
        sim.openMagnetMissing = rng.chance(800);
        sim.closedMagnetMissing = rng.chance(800);
        sim.currentFitted = !rng.chance(300);
        sim.runningAmps = rng.uniform(1.0f, s.maxCurrentAmps * (rng.chance(1000) ? 1.5f : 0.95f));
        const uint32_t start = rng.below(10);
        sim.positionMm = start < 4 ? 0.0f : (start < 7 ? sim.travelMm : rng.uniform(0.0f, sim.travelMm));
        return sim;
    }

    // The observer: what it has seen, to check the properties independently of
    // the controller's own bookkeeping.
    struct Observer
    {
        Settings settings;
        uint32_t seed = 0;
        int step = 0;
        bool wasOn = false;
        bool everOn = true;     // start-up counts as a stop (constitution I)
        bool pendingOff = true; // the off time starts at the next step
        uint8_t railHistory = 0;
        uint32_t drivenSinceMagnetMs[2] = {0, 0}; // toward each end (index: End)
        End onTarget = End::Closed;
        Driver::Command lastDirection = Driver::Command::Off;
        uint32_t offSinceMs = 0;
        uint32_t onSinceMs = 0;
        uint32_t onTimeoutMs = 0;
        bool onFromStartMagnet = false;
        bool onLeftStart = false;
        bool needsPerson = false; // P4: position unknown at rest, and no person's command since
        bool faultSeen = false;

        void fail(const char *property) const
        {
            char message[160];
            snprintf(message, sizeof(message), "%s broken: seed %u, step %d", property, static_cast<unsigned>(seed), step);
            TEST_FAIL_MESSAGE(message);
        }

        bool magnet(HallLevel level, End &end) const
        {
            if (level != HallLevel::High && level != HallLevel::Low)
                return false;
            const End other = settings.hallHighEnd == End::Open ? End::Closed : End::Open;
            end = level == HallLevel::High ? settings.hallHighEnd : other;
            return true;
        }

        // P1, P2: the output on its own, and against the target.
        void checkOutput(const Controller &roof) const
        {
            const Output out = roof.output();
            const Driver::Outputs pins = Driver::outputsFor(out.command, out.duty);
            if (pins.rpwm > 0 && pins.lpwm > 0)
                fail("P1 never both directions");
            const bool on = out.command != Driver::Command::Off;
            if (!on && out.duty != 0)
                fail("P1 off means duty 0");
            if (!on)
                return;
            const Status &st = roof.status();
            if (st.state != State::Opening && st.state != State::Closing)
                fail("P1 driving only while opening or closing");
            const Driver::Command openDir = settings.openDirection;
            const Driver::Command closeDir = openDir == Driver::Command::Forward ? Driver::Command::Reverse : Driver::Command::Forward;
            if (out.command != (st.target == End::Open ? openDir : closeDir))
                fail("P1 direction matches the target");
            if (out.duty > toDuty(dutyFraction(settings.topDutyPercent)))
                fail("P2 duty above the top duty");
            if (st.fromUnknown && out.duty > toDuty(dutyFraction(settings.slowDutyPercent)))
                fail("P2 duty above the slow duty from unknown");
        }

        // P1 dead time, P5 timeouts, P6 the target magnet; after a step or a command.
        void checkPeriods(const Controller &roof, uint32_t nowMs)
        {
            const Output out = roof.output();
            const Status &st = roof.status();
            const bool on = out.command != Driver::Command::Off;
            if (on && !wasOn)
                startPeriod(roof, nowMs);
            if (!on && wasOn)
                pendingOff = true;
            wasOn = on;
            if (on)
                onTarget = st.target;
            if (!on)
                return;
            End at = End::Closed;
            const bool atMagnet = magnet(st.hall, at);
            if (atMagnet && at == st.target)
                fail("P6 the target magnet ends the move");
            if (!atMagnet)
                onLeftStart = true;
            if (nowMs - onSinceMs >= onTimeoutMs)
                fail("P5 a drive outlasted its timeout");
            if (onFromStartMagnet && !onLeftStart && nowMs - onSinceMs >= settings.leaveMagnetMs)
                fail("P5 a drive stayed on its start magnet past the leave time");
        }

        void startPeriod(const Controller &roof, uint32_t nowMs)
        {
            const Output out = roof.output();
            const Status &st = roof.status();
            if (everOn && nowMs - offSinceMs < DEAD_TIME_MS)
                fail("P1 off for the dead time between drives");
            everOn = true;
            lastDirection = out.command;
            onSinceMs = nowMs;
            onTimeoutMs = st.moveTimeoutMs;
            // The timeout is the profile's (the controller can't hand out a longer one).
            const uint32_t bound =
                st.fromUnknown ? unknownMoveTimeoutMs(settings) : moveTimeoutMs(settings, static_cast<float>(settings.travelMm));
            if (onTimeoutMs == 0 || onTimeoutMs > bound)
                fail("P5 the timeout is within the profile's bound");
            End at = End::Closed;
            onFromStartMagnet = magnet(st.hall, at) && at != st.target;
            onLeftStart = false;
        }

        // P3, P4 after a step or a command.
        void checkLatchAndUnknown(const Controller &roof)
        {
            const Status &st = roof.status();
            const bool on = roof.output().command != Driver::Command::Off;
            if (st.fault.kind != FaultKind::None)
                faultSeen = true;
            if (faultSeen && on)
                fail("P3 a latched fault keeps the driver off");
            if (st.fault.kind != FaultKind::None && st.state != State::Error)
                fail("P3 a fault is the error state");
            // At rest the magnets decide: a known position allows any origin, an
            // unknown one needs a person's command before anything drives.
            const bool moving = st.state == State::Opening || st.state == State::Closing;
            if (!moving)
                needsPerson = !st.positionKnown;
            if (needsPerson && on)
                fail("P4 no motion from unknown without a person");
            if (st.positionKnown && (st.positionMm < 0.0f || st.positionMm > static_cast<float>(settings.travelMm)))
                fail("position within the travel");
        }

        // P6 also means the move ends *at its end*, not as a fault: a step that
        // confirms the target magnet of a move in progress leaves the roof there
        // (unless the step itself was late, which is checked first).
        void checkArrival(const Controller &roof, State before, End target) const
        {
            if (before != State::Opening && before != State::Closing)
                return;
            const Status &st = roof.status();
            End at = End::Closed;
            if (!magnet(st.hall, at) || at != target || st.fault.kind == FaultKind::LateStep)
                return;
            if (st.state != (target == End::Open ? State::Open : State::Closed))
                fail("P6 reaching the target magnet ends the move at that end");
        }

        // Before the step's checks: the dead time runs from the first step after
        // the driver went off; the drive budget counts the gap if it was on.
        void beforeStep(uint32_t gapMs)
        {
            if (wasOn)
                drivenSinceMagnetMs[static_cast<int>(onTarget)] += gapMs;
        }

        // P7, P8 after a step, with the Hall reading that was fed to it.
        void afterStep(const Controller &roof, uint32_t nowMs, uint16_t hallCounts)
        {
            if (pendingOff)
            {
                offSinceMs = nowMs;
                pendingOff = false;
            }
            const Status &st = roof.status();
            const bool rail = hallCounts <= HALL_RAIL_LOW_COUNTS || hallCounts >= HALL_RAIL_HIGH_COUNTS;
            railHistory = static_cast<uint8_t>((railHistory << 1) | (rail ? 1 : 0));
            int rails = 0;
            for (int bit = 0; bit < 8; ++bit)
                rails += (railHistory >> bit) & 1;
            if (rails >= 3 && st.fault.kind == FaultKind::None)
                fail("P7 three rail readings in eight latch a fault");
            if (st.hall == HallLevel::High || st.hall == HallLevel::Low)
                drivenSinceMagnetMs[0] = drivenSinceMagnetMs[1] = 0;
            const bool on = roof.output().command != Driver::Command::Off;
            if (on && drivenSinceMagnetMs[static_cast<int>(st.target)] >= driveBudgetMs(settings))
                fail("P8 total driving without a magnet stays under the budget");
        }

        void check(const Controller &roof, uint32_t nowMs)
        {
            checkOutput(roof);
            checkPeriods(roof, nowMs);
            checkLatchAndUnknown(roof);
        }

        // What a command may change in the observer's view.
        void commanded(const Controller &roof, Command command, Origin origin, const CommandResult &result)
        {
            const bool move = command == Command::Open || command == Command::Close;
            if (move && result.accepted && origin == Origin::Person)
                needsPerson = false;
            (void)roof;
        }

        void cleared(const Controller &roof, Origin origin)
        {
            if (roof.status().fault.kind != FaultKind::None)
                return;
            if (origin != Origin::Person && faultSeen)
                fail("P3 only a person clears a fault");
            if (faultSeen)
                drivenSinceMagnetMs[0] = drivenSinceMagnetMs[1] = 0;
            faultSeen = false;
        }
    };

    // Random glitches injected into the readings.
    struct Glitches
    {
        int railSteps = 0;
        uint16_t railCounts = 0;
        int dipSteps = 0;
        float dipVolts = 12.6f;
        int spikeSteps = 0;
        int flickerSteps = 0; // an intermittent connector: every other reading at a rail
    };

    Readings readingsWith(const RoofSim &sim, uint32_t nowMs, Glitches &g, Random &rng)
    {
        Readings r = sim.readings(nowMs);
        const int noise = static_cast<int>(rng.below(31)) - 15;
        r.hallCounts = static_cast<uint16_t>(std::max(0, std::min(4095, r.hallCounts + noise)));
        if (g.railSteps > 0)
        {
            --g.railSteps;
            r.hallCounts = g.railCounts;
        }
        if (g.flickerSteps > 0)
        {
            --g.flickerSteps;
            if (g.flickerSteps % 2 == 0)
                r.hallCounts = 4095;
        }
        if (g.dipSteps > 0)
        {
            --g.dipSteps;
            r.supplyVolts = g.dipVolts;
        }
        if (g.spikeSteps > 0)
        {
            --g.spikeSteps;
            r.motorAmps = 40.0f;
        }
        return r;
    }

    void maybeGlitch(Glitches &g, RoofSim &sim, Random &rng)
    {
        if (rng.chance(3))
        {
            g.railSteps = static_cast<int>(rng.between(1, 8));
            g.railCounts = rng.chance(5000) ? static_cast<uint16_t>(rng.below(31)) : static_cast<uint16_t>(rng.between(4065, 4095));
        }
        if (rng.chance(5))
        {
            g.dipSteps = static_cast<int>(rng.between(1, 30));
            g.dipVolts = rng.chance(1000) ? NAN : rng.uniform(7.0f, 12.5f);
        }
        if (rng.chance(3))
            g.flickerSteps = static_cast<int>(rng.between(2, 12));
        if (rng.chance(5))
            g.spikeSteps = static_cast<int>(rng.between(1, 20));
        if (rng.chance(5))
            sim.jammed = !sim.jammed;
    }

    uint32_t randomGapMs(Random &rng)
    {
        const uint32_t roll = rng.below(10000);
        if (roll < 9500)
            return 20;
        if (roll < 9998)
            return rng.between(1, 99);
        return rng.between(100, 160);
    }

    void randomCommand(Controller &roof, Observer &obs, Random &rng, uint32_t nowMs)
    {
        const Origin origin = rng.chance(5000) ? Origin::Person : Origin::Automatic;
        const uint32_t pick = rng.below(10);
        if (pick < 1)
        {
            roof.clearFault(origin);
            obs.cleared(roof, origin);
            return;
        }
        if (pick < 2)
        {
            Settings s = randomSettings(rng);
            s.hallHighEnd = obs.settings.hallHighEnd; // the magnets don't move
            s.openDirection = obs.settings.openDirection;
            s.travelMm = obs.settings.travelMm;
            s.hallHighCounts = obs.settings.hallHighCounts;
            s.hallLowCounts = obs.settings.hallLowCounts;
            if (roof.applySettings(s).accepted)
                obs.settings = s;
            return;
        }
        static const Command COMMANDS[] = {
            Command::Open, Command::Close, Command::Open, Command::Close, Command::Stop, Command::Abort, Command::Open, Command::Close};
        const Command command = COMMANDS[pick - 2];
        const CommandResult result = roof.command(command, origin);
        obs.commanded(roof, command, origin, result);
    }

    void runSequence(uint32_t seed)
    {
        Random rng(seed);
        Observer obs;
        obs.seed = seed;
        obs.settings = randomSettings(rng);
        RoofSim sim = simFor(obs.settings, rng);
        Controller roof(obs.settings);
        Glitches glitches;
        uint32_t nowMs = rng.next();
        if (rng.chance(300))
            roof.raiseFault(Fault{FaultKind::Timeout, 1.0f, 1.0f});
        for (obs.step = 0; obs.step < STEPS_PER_SEQUENCE; ++obs.step)
        {
            if (rng.chance(150))
            {
                randomCommand(roof, obs, rng, nowMs);
                obs.check(roof, nowMs);
            }
            maybeGlitch(glitches, sim, rng);
            const uint32_t gapMs = randomGapMs(rng);
            sim.drive(roof.output(), gapMs);
            nowMs += gapMs;
            const State before = roof.status().state;
            const End target = roof.status().target;
            const Readings readings = readingsWith(sim, nowMs, glitches, rng);
            obs.beforeStep(gapMs);
            roof.tick(readings);
            obs.afterStep(roof, nowMs, readings.hallCounts);
            obs.check(roof, nowMs);
            obs.checkArrival(roof, before, target);
        }
    }

    // How often the sequences reached each situation, so the test can't pass by
    // never getting there.
    struct Coverage
    {
        uint32_t arrived = 0;
        uint32_t faults[11] = {};
        uint32_t unknownMoves = 0;
    };
} // namespace

void test_property_every_sequence_keeps_the_safety_rules(void)
{
    for (uint32_t seed = 1; seed <= SEQUENCES; ++seed)
        runSequence(seed);
}

// The sequences must actually reach the situations the properties are about.
void test_property_sequences_reach_every_fault_and_both_ends(void)
{
    Coverage seen;
    for (uint32_t seed = 1; seed <= 2000; ++seed)
    {
        Random rng(seed);
        Settings s = randomSettings(rng);
        RoofSim sim = simFor(s, rng);
        Controller roof(s);
        Glitches glitches;
        uint32_t nowMs = rng.next();
        FaultKind lastFault = FaultKind::None;
        for (int step = 0; step < STEPS_PER_SEQUENCE; ++step)
        {
            if (rng.chance(150))
            {
                Observer scratch;
                scratch.settings = s;
                randomCommand(roof, scratch, rng, nowMs);
                s = scratch.settings;
            }
            maybeGlitch(glitches, sim, rng);
            const uint32_t gapMs = randomGapMs(rng);
            sim.drive(roof.output(), gapMs);
            nowMs += gapMs;
            const State before = roof.status().state;
            roof.tick(readingsWith(sim, nowMs, glitches, rng));
            const Status &st = roof.status();
            if ((before == State::Opening || before == State::Closing) && (st.state == State::Open || st.state == State::Closed))
                ++seen.arrived;
            if (st.fault.kind != lastFault && st.fault.kind != FaultKind::None)
                ++seen.faults[static_cast<int>(st.fault.kind)];
            lastFault = st.fault.kind;
            if (st.fromUnknown && st.driving)
                ++seen.unknownMoves;
        }
    }
    printf(
        "coverage: %u arrivals, %u steps driving from unknown, faults:",
        static_cast<unsigned>(seen.arrived),
        static_cast<unsigned>(seen.unknownMoves));
    for (int kind = 1; kind < 11; ++kind)
        printf(" %s %u", faultName(static_cast<FaultKind>(kind)), static_cast<unsigned>(seen.faults[kind]));
    printf("\n");
    TEST_ASSERT_TRUE(seen.arrived > 100);
    TEST_ASSERT_TRUE(seen.unknownMoves > 100);
    for (FaultKind kind :
         {FaultKind::Timeout,
          FaultKind::DidNotLeave,
          FaultKind::WrongMagnet,
          FaultKind::HallRail,
          FaultKind::OverCurrent,
          FaultKind::CurrentUnreadable,
          FaultKind::SupplyLow,
          FaultKind::LateStep})
    {
        char message[64];
        snprintf(message, sizeof(message), "never reached %s", faultName(kind));
        TEST_ASSERT_TRUE_MESSAGE(seen.faults[static_cast<int>(kind)] > 0, message);
    }
}

// P8b: a free roof (no jam, both magnets, real speed as set) opened or closed
// part-way for a random time and reversed once to its start magnet ends there
// with no fault, for random settings including slow = top and the tightest margin.
void test_property_one_reversal_never_trips_the_drive_budget(void)
{
    uint32_t reversals = 0;
    for (uint32_t seed = 1; seed <= 400; ++seed)
    {
        Random rng(seed + 900000);
        Settings s = randomSettings(rng);
        s.travelMm = rng.between(300, 1500);
        if (rng.chance(3000))
            s.slowDutyPercent = s.topDutyPercent;
        if (rng.chance(3000))
            s.timeoutMarginMs = 3000;
        RoofTest::Bench b(s, rng.chance(5000) ? 0.0f : static_cast<float>(s.travelMm));
        b.sim.reversedWiring = s.openDirection == Driver::Command::Reverse;
        b.sim.highEndClosed = s.hallHighEnd == End::Closed;
        b.sim.highMagnetCounts = static_cast<uint16_t>(s.hallHighCounts + 300);
        b.sim.lowMagnetCounts = static_cast<uint16_t>(s.hallLowCounts - 300);
        b.settleHall();
        const bool fromClosed = b.state() == State::Closed;
        b.command(fromClosed ? Command::Open : Command::Close);
        const uint32_t legMs = rng.between(1000, moveTimeoutMs(s, static_cast<float>(s.travelMm)));
        const State moving = fromClosed ? State::Opening : State::Closing;
        for (uint32_t t = 0; t < legMs && b.state() == moving; t += b.stepMs)
            b.step();
        if (b.state() != moving)
            continue; // reached the far end: no reversal part-way
        b.command(fromClosed ? Command::Close : Command::Open);
        ++reversals;
        b.runToRest(3000000);
        char message[96];
        snprintf(
            message,
            sizeof(message),
            "seed %u: ended %s with fault %s",
            static_cast<unsigned>(seed),
            stateName(b.state()),
            faultName(b.roof.status().fault.kind));
        TEST_ASSERT_TRUE_MESSAGE(b.state() == (fromClosed ? State::Closed : State::Open), message);
    }
    TEST_ASSERT_TRUE(reversals > 200); // most sequences really reverse part-way
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_property_every_sequence_keeps_the_safety_rules);
    RUN_TEST(test_property_sequences_reach_every_fault_and_both_ends);
    RUN_TEST(test_property_one_reversal_never_trips_the_drive_budget);
    return UNITY_END();
}
