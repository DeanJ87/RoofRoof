// Spec 014 SC-004: spec 005's safety properties (test_close_properties, P1 to
// P11) with every policy value drawn at random from its range, the not-started
// policy either way, and the bounds of each property taken from the policy in
// force. Whatever the owner sets within the ranges, the sequence never opens,
// never closes without the park held for the hold time, respects a stand-down
// for the re-arm spell, alerts every critical event and never goes quiet while
// unsafe and open.
//
// A failure names the seed and the step, so it can be replayed.

#include <cstdio>
#include <unity.h>

#include "CloseSequence.h"
#include "Config.h" // for the library finder: RoofLogic (Roof::State) builds with ConfigModel

using namespace SQM;
using namespace SQM::CloseSeq;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t SEQUENCES = 5000;
    constexpr int STEPS_PER_SEQUENCE = 3000;
    constexpr uint32_t MOVE_TIMEOUT_MS = 60000;

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
        bool chance(uint32_t perThousand) { return below(1000) < perThousand; }
    };

    // The roof and the world around it, as the sequence sees them.
    struct World
    {
        Inputs in;
        uint64_t closeEndsAtMs = 0;   // a running close reaches the magnet
        uint64_t pendingEndsAtMs = 0; // a waiting close starts
        bool pendingStalls = false;   // the waiting close never starts
        bool closeHangs = false;      // the close never reaches the magnet (the logic misbehaves)
        uint32_t weatherDwellMs = 0;  // how long the verdict stays as it is
        uint64_t weatherChangedMs = 0;
    };

    // The observer's own record.
    struct Watch
    {
        bool parkedRun = false;
        uint64_t parkedSinceMs = 0;
        bool safeRun = false;
        uint64_t safeSinceMs = 0;
        bool anyRequest = false;
        uint64_t lastRequestMs = 0;
        bool held = false;
        bool attemptOn = false;
        uint64_t attemptSinceMs = 0;
        uint32_t attemptLimitMs = DEFAULT_PARK_TIMEOUT_MS;
        Policy policy; // the policy in force (clamped, as the sequence holds it)
        bool closingRun = false;
        uint64_t closingSinceMs = 0;
        bool alertedEver[8][11] = {};
        uint64_t alertedAtMs[8][11] = {};
        bool halted = false;          // a close outlasted its guard
        uint64_t closeBarUntilMs = 0; // a refused or interrupted close: no close before this
        uint64_t quietUnsafeMs = 0;   // unsafe and not closed, with nothing done or raised
        uint64_t lastStepMs = 0;
        bool prevUnsafe = false;
        State lastRoof = State::Unknown;
        bool closedByMove = false; // the roof went from closing to closed this step
    };

    struct Failure
    {
        uint32_t seed;
        int step;
        char what[160];
    };

    bool movable(State state)
    {
        return state == State::Open || state == State::Stopped || state == State::Opening;
    }

    void advanceWeather(World &w, Random &rnd)
    {
        if (w.in.nowMs - w.weatherChangedMs < w.weatherDwellMs)
            return;
        w.in.weatherSafe = !w.in.weatherSafe;
        w.weatherChangedMs = w.in.nowMs;
        // Mostly long spells, sometimes flapping every second or so.
        w.weatherDwellMs = rnd.chance(300) ? 200 + rnd.below(2000) : 5000 + rnd.below(900000);
    }

    // A person's stop or abort, from anywhere (REST, later Alpaca or MQTT).
    void personStops(World &w, Random &rnd)
    {
        Inputs &in = w.in;
        if (!rnd.chance(2) || in.roof == State::Closed)
            return;
        in.personStops++;
        if (in.roof == State::Closing || in.roof == State::Opening)
            in.roof = State::Stopped;
        in.closePending = false;
    }

    // A person's close, open, fault clear or move from unknown.
    void personMoves(World &w, Random &rnd)
    {
        Inputs &in = w.in;
        if (rnd.chance(1) && in.parked && movable(in.roof))
        {
            in.roof = State::Closing;
            in.moveTimeoutMs = MOVE_TIMEOUT_MS;
            w.closeEndsAtMs = in.nowMs + 5000 + rnd.below(50000);
            w.closeHangs = false;
        }
        if (rnd.chance(1) && in.weatherSafe && (in.roof == State::Closed || in.roof == State::Stopped))
            in.roof = State::Opening;
        if (rnd.chance(2) && in.roof == State::Error)
            in.roof = rnd.chance(500) ? State::Unknown : State::Open; // a person cleared the fault
        if (rnd.chance(2) && in.roof == State::Unknown)
            in.roof = rnd.chance(500) ? State::Closed : State::Open; // a person moved it to a magnet
    }

    void roofRuns(World &w, Random &rnd)
    {
        Inputs &in = w.in;
        if (in.roof == State::Opening && (!in.weatherSafe || rnd.chance(20)))
            in.roof = in.weatherSafe ? State::Open : State::Stopped; // spec 003 stops an open when unsafe
        if (in.closePending && !w.pendingStalls && in.nowMs >= w.pendingEndsAtMs)
        {
            in.closePending = false;
            in.roof = State::Closing;
            in.moveTimeoutMs = MOVE_TIMEOUT_MS;
            w.closeEndsAtMs = in.nowMs + 5000 + rnd.below(50000);
        }
        if (in.roof != State::Closing)
            return;
        if (!in.parked)
            in.roof = State::Error; // spec 003's park interlock
        else if (rnd.chance(1))
            in.roof = State::Error; // a jam or 12 V low
        else if (rnd.chance(1))
            in.roof = State::Stopped; // stopped by something other than a person (a waiting close dropped)
        else if (!w.closeHangs && in.nowMs >= w.closeEndsAtMs)
            in.roof = State::Closed;
        if (in.roof != State::Closing)
            in.moveTimeoutMs = 0;
    }

    void worldChanges(World &w, Random &rnd)
    {
        advanceWeather(w, rnd);
        if (rnd.chance(15))
            w.in.parked = !w.in.parked;
        if (rnd.chance(1))
            w.in.ninaConfigured = !w.in.ninaConfigured;
        if (rnd.chance(1) && w.in.roof != State::Closing)
            w.in.roof = rnd.chance(500) ? State::Unknown : State::Error; // a restart between magnets, a fault at rest
        personStops(w, rnd);
        personMoves(w, rnd);
        roofRuns(w, rnd);
    }

    // The roof's answer to the sequence's close, as spec 003 would give it.
    CloseAnswer answerClose(World &w, Random &rnd)
    {
        Inputs &in = w.in;
        if (!in.parked)
            return CloseAnswer::ParkRefused;
        if (rnd.chance(100))
            return CloseAnswer::Refused; // supply low, settings invalid
        if (rnd.chance(150))
        {
            in.closePending = true;
            w.pendingStalls = rnd.chance(300);
            w.pendingEndsAtMs = in.nowMs + 500 + rnd.below(8000);
            return CloseAnswer::Started;
        }
        in.roof = State::Closing;
        in.moveTimeoutMs = MOVE_TIMEOUT_MS;
        w.closeEndsAtMs = in.nowMs + 5000 + rnd.below(50000);
        w.closeHangs = rnd.chance(50);
        return CloseAnswer::Started;
    }

    void carryOut(World &w, Action action, Sequence &seq, Random &rnd, Decision &answer)
    {
        if (action == Action::Close)
            answer = seq.closeAnswered(answerClose(w, rnd));
        if (action == Action::Stop)
        {
            if (w.in.roof == State::Closing)
                w.in.roof = State::Stopped;
            w.in.closePending = false;
            w.in.moveTimeoutMs = 0;
        }
    }

    bool fail(Failure &f, uint32_t seed, int step, const char *what)
    {
        f.seed = seed;
        f.step = step;
        snprintf(f.what, sizeof(f.what), "%s", what);
        return false;
    }

    // Updates the observer's runs for this step's inputs (before the decision).
    void observeInputs(Watch &watch, const Inputs &in)
    {
        watch.closedByMove = watch.lastRoof == State::Closing && in.roof == State::Closed;
        watch.lastRoof = in.roof;
        if (!in.parked)
            watch.parkedRun = false;
        else if (!watch.parkedRun)
        {
            watch.parkedRun = true;
            watch.parkedSinceMs = in.nowMs;
        }
        if (!in.weatherSafe)
        {
            // A safe spell of 10 min or more (seen only when it ends, between steps) starts afresh.
            if (watch.safeRun && in.nowMs - watch.safeSinceMs >= watch.policy.rearmSafeMs)
                watch.quietUnsafeMs = 0;
            watch.safeRun = false;
        }
        else if (!watch.safeRun)
        {
            watch.safeRun = true;
            watch.safeSinceMs = in.nowMs;
        }
        if (in.roof == State::Closed || (watch.safeRun && in.nowMs - watch.safeSinceMs >= watch.policy.rearmSafeMs))
        {
            watch.held = false;
            watch.halted = false;
        }
    }

    bool running(Phase phase)
    {
        return phase == Phase::WaitPark || phase == Phase::Closing || phase == Phase::RetryWait;
    }

    // One decision with the phases around it.
    struct Seen
    {
        Decision decision;
        Phase before = Phase::Idle;
        Phase after = Phase::Idle;
    };

    // P2: a close only when it is allowed. nullptr when it holds.
    const char *checkClose(const Inputs &in, const Watch &watch)
    {
        if (in.weatherSafe)
            return "P2 close while safe";
        if (!watch.parkedRun || in.nowMs - watch.parkedSinceMs < watch.policy.parkSettleMs)
            return "P2 close without 4 s of park";
        if (!movable(in.roof) || in.closePending)
            return "P2 close of a roof that isn't movable";
        if (in.nowMs < watch.closeBarUntilMs)
            return "P10 a close within the back-off after a refused or interrupted close";
        return nullptr;
    }

    // P3: park requests spaced and only while not safe.
    const char *checkRequest(const Inputs &in, const Watch &watch)
    {
        if (in.weatherSafe)
            return "P3 park request while safe";
        if (watch.anyRequest && in.nowMs - watch.lastRequestMs < watch.policy.parkRequestGapMs)
            return "P3 park requests closer than 30 s";
        return nullptr;
    }

    // P1-P5 and P8's stop rule for one decision.
    const char *checkAction(const Seen &seen, const Inputs &in, const Watch &watch)
    {
        const Action action = seen.decision.action;
        if (action == Action::None)
            return nullptr;
        if (action != Action::RequestPark && action != Action::Close && action != Action::Stop)
            return "P1 an action other than request, close or stop";
        if (watch.held)
            return "P4 an action while stood down by a person";
        if (watch.halted)
            return "P9 an action after a close outlasted its guard";
        if (in.roof == State::Unknown || in.roof == State::Error)
            return "P5 an action while unknown or faulted";
        if (action == Action::Close)
            return checkClose(in, watch);
        if (action == Action::RequestPark)
            return checkRequest(in, watch);
        return seen.before == Phase::Closing ? nullptr : "P8 a stop outside a close";
    }

    // P6: an attempt never outlasts its park limit.
    const char *checkAttempt(const Seen &seen, const Inputs &in, Watch &watch)
    {
        if (seen.decision.action == Action::RequestPark)
        {
            watch.anyRequest = true;
            watch.lastRequestMs = in.nowMs;
        }
        if (seen.after == Phase::WaitPark && seen.before != Phase::WaitPark && seen.decision.event == Event::Started)
        {
            watch.attemptOn = true;
            watch.attemptSinceMs = in.nowMs;
        }
        if (seen.after != Phase::WaitPark && seen.after != Phase::Closing)
            watch.attemptOn = false;
        // A parked mount held back only by the back-off after a failed close isn't a park failure.
        const bool heldBack =
            watch.parkedRun && in.nowMs - watch.parkedSinceMs >= watch.policy.parkSettleMs && in.nowMs < watch.closeBarUntilMs;
        // An unknown position pauses the attempt for up to 2 s before it stands down.
        const bool pausedUnknown = in.roof == State::Unknown;
        if (seen.after == Phase::WaitPark && watch.attemptOn && !heldBack && !pausedUnknown &&
            in.nowMs - watch.attemptSinceMs > watch.attemptLimitMs)
            return "P6 waited past the park limit without failing the attempt";
        return nullptr;
    }

    // P7: each critical event alerts once per episode.
    const char *checkAlert(const Seen &seen, const Inputs &in, Watch &watch)
    {
        // A closed roof ends the trouble: the throttle starts afresh.
        if (watch.closedByMove)
        {
            watch.closedByMove = false;
            for (auto &row : watch.alertedEver)
                for (bool &ever : row)
                    ever = false;
        }
        if (seen.decision.alert)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(seen.decision.event));
            const size_t e = static_cast<size_t>(seen.decision.event);
            const size_t c = static_cast<size_t>(seen.decision.cause);
            (void)bit;
            // The throttle: the reminder interval, at most spec 005's 30 minutes.
            const uint64_t throttleMs = watch.policy.remindMs < REMIND_MS ? watch.policy.remindMs : REMIND_MS;
            if (watch.alertedEver[e][c] && in.nowMs - watch.alertedAtMs[e][c] < throttleMs)
                return "P7 a critical event and cause alerted twice within 30 minutes";
            watch.alertedEver[e][c] = true;
            watch.alertedAtMs[e][c] = in.nowMs;
        }
        const bool failed = seen.decision.event == Event::CloseFailed && !seen.decision.reminder;
        if (failed && seen.decision.cause == Cause::TooLong)
            watch.halted = true;
        if (failed && (seen.decision.cause == Cause::Refused || seen.decision.cause == Cause::Interrupted ||
                       seen.decision.cause == Cause::NotStarted))
            watch.closeBarUntilMs = in.nowMs + watch.policy.retryAfterMs;
        return nullptr;
    }

    // P8: a close in motion never outlasts its guard (plus the observer's step).
    const char *checkGuard(const Seen &seen, const Inputs &in, Watch &watch)
    {
        if (seen.after != Phase::Closing || in.roof != State::Closing)
        {
            watch.closingRun = false;
            return nullptr;
        }
        if (!watch.closingRun)
        {
            watch.closingRun = true;
            watch.closingSinceMs = in.nowMs;
        }
        if (in.nowMs - watch.closingSinceMs > in.moveTimeoutMs + watch.policy.closeGuardSlackMs + 3100)
            return "P8 a close outlasted its guard";
        return nullptr;
    }

    // P6-P8 and the observer's bookkeeping after a decision.
    const char *checkAfter(const Seen &seen, const Inputs &in, Watch &watch)
    {
        const char *broken = checkAttempt(seen, in, watch);
        if (broken == nullptr)
            broken = checkAlert(seen, in, watch);
        if (broken == nullptr)
            broken = checkGuard(seen, in, watch);
        return broken;
    }

    bool standingDown(Phase phase)
    {
        return phase == Phase::HeldByPerson || phase == Phase::CannotMove || phase == Phase::Halted;
    }

    // P11: unsafe with the roof open never goes quiet for longer than an
    // attempt and its back-off: something is closed, raised or stood down.
    const char *checkLiveness(const Decision &d, const Decision &answer, const Inputs &in, Phase after, Watch &watch)
    {
        const uint64_t stepMs = in.nowMs - watch.lastStepMs;
        const bool prevUnsafe = watch.prevUnsafe;
        watch.prevUnsafe = !in.weatherSafe;
        watch.lastStepMs = in.nowMs;
        const bool acted = d.action == Action::Close || isCritical(d.event) || isCritical(answer.event);
        const bool safeLong = watch.safeRun && in.nowMs - watch.safeSinceMs >= watch.policy.rearmSafeMs;
        if (acted || safeLong || in.roof == State::Closed || in.roof == State::Closing || in.closePending || standingDown(after))
        {
            watch.quietUnsafeMs = 0;
            return nullptr;
        }
        // Only time between two unsafe samples counts (a step can straddle a safe spell).
        if (!in.weatherSafe && prevUnsafe)
            watch.quietUnsafeMs += stepMs;
        if (watch.quietUnsafeMs > watch.attemptLimitMs + watch.policy.retryAfterMs + watch.policy.parkSettleMs + 200000)
            return "P11 unsafe and open with no close, alert or stand-down for longer than an attempt and its back-off";
        return nullptr;
    }

    // A random starting world and, sometimes, a park time limit other than the default.
    uint64_t between(Random &rnd, const PolicyRange &range)
    {
        // Mostly anywhere in the range, sometimes exactly at either end.
        const uint32_t pick = rnd.below(10);
        if (pick == 0)
            return range.minMs;
        if (pick == 1)
            return range.maxMs;
        return range.minMs + rnd.below(static_cast<uint32_t>(range.maxMs - range.minMs + 1));
    }

    Policy randomPolicy(Random &rnd)
    {
        Policy p;
        p.parkRequestGapMs = between(rnd, PARK_REQUEST_GAP_RANGE);
        p.parkSettleMs = between(rnd, PARK_SETTLE_RANGE);
        p.retryAfterMs = between(rnd, RETRY_AFTER_RANGE);
        p.rearmSafeMs = between(rnd, REARM_SAFE_RANGE);
        p.closeStartMs = between(rnd, CLOSE_START_RANGE);
        p.closeGuardSlackMs = between(rnd, CLOSE_GUARD_SLACK_RANGE);
        p.remindMs = between(rnd, REMIND_RANGE);
        p.retryNotStarted = rnd.chance(500);
        return p;
    }

    void startWorld(World &w, Watch &watch, Sequence &seq, Random &rnd)
    {
        watch.policy = randomPolicy(rnd);
        seq.setPolicy(watch.policy);
        if (rnd.chance(300))
        {
            watch.attemptLimitMs = 30000 + rnd.below(240000);
            seq.setParkTimeoutMs(watch.attemptLimitMs);
        }
        w.in.nowMs = rnd.below(100000);
        w.in.weatherSafe = rnd.chance(500);
        const State starts[] = {State::Open, State::Closed, State::Unknown, State::Stopped, State::Error};
        w.in.roof = starts[rnd.below(5)];
        w.in.parked = rnd.chance(300);
        w.in.ninaConfigured = !rnd.chance(50);
    }

    // A restart of the board (a reset, a crash, a brown-out): only what the
    // sequence keeps survives; a move under way ends between the magnets.
    void restartBoard(World &w, Watch &watch, Sequence &seq, uint32_t &lastStops)
    {
        const Sequence::Saved saved = seq.saved();
        seq = Sequence();
        seq.setParkTimeoutMs(watch.attemptLimitMs);
        seq.setPolicy(watch.policy);
        seq.restore(saved);
        if (w.in.roof == State::Closing || w.in.roof == State::Opening)
            w.in.roof = State::Unknown;
        w.in.closePending = false;
        w.in.personStops = 0;
        lastStops = 0;
        // What lives only in RAM starts again: the alert throttle and the close back-off.
        for (auto &row : watch.alertedEver)
            for (bool &ever : row)
                ever = false;
        // A restart during a close counts as a failed close (spec 005 item
        // 11j): the restored sequence backs off from the restart, raising the
        // failure as a reminder, so the observer starts the bar here.
        watch.closeBarUntilMs = saved.phase == Phase::Closing ? w.in.nowMs + watch.policy.retryAfterMs : 0;
        watch.closingRun = false;
        // The time the board was down (since the last step) doesn't count: it
        // ran no clock. (On the bench a restart takes a few seconds.)
        const uint64_t downMs = w.in.nowMs - watch.lastStepMs;
        watch.attemptSinceMs += downMs;
        watch.lastStepMs = w.in.nowMs;
    }

    // One step of the sequence, the roof's answer and every check. nullptr when all hold.
    const char *stepOnce(World &w, Watch &watch, Sequence &seq, Random &rnd, uint32_t &lastStops)
    {
        w.in.nowMs += rnd.chance(20) ? 30000 + rnd.below(120000) : 10 + rnd.below(1500);
        if (rnd.chance(3))
            restartBoard(w, watch, seq, lastStops);
        worldChanges(w, rnd);
        observeInputs(watch, w.in);
        const Phase before = seq.phase();
        // A person's stop seen while the sequence runs stands it down (P4),
        // unless the roof reads closed, which ends it.
        const bool safeLong = watch.safeRun && w.in.nowMs - watch.safeSinceMs >= watch.policy.rearmSafeMs;
        const bool watching = running(before) || (before == Phase::CannotMove && !safeLong);
        const bool stopped = w.in.personStops != lastStops && watching && w.in.roof != State::Closed;
        lastStops = w.in.personStops;
        const Decision d = seq.step(w.in);
        if (stopped && d.event != Event::StoppedByPerson)
            return "P4 a person's stop didn't stand the sequence down";
        if (stopped)
            watch.held = true;
        const Seen seen{d, before, seq.phase()};
        const char *broken = checkAction(seen, w.in, watch);
        if (broken == nullptr)
            broken = checkAfter(seen, w.in, watch);
        if (broken != nullptr)
            return broken;
        Decision answer;
        carryOut(w, d.action, seq, rnd, answer);
        broken = checkAfter(Seen{answer, Phase::Closing, seq.phase()}, w.in, watch);
        if (broken == nullptr)
            broken = checkLiveness(d, answer, w.in, seq.phase(), watch);
        return broken;
    }

    bool runOne(uint32_t seed, Failure &f)
    {
        Random rnd(seed);
        Sequence seq;
        Watch watch;
        World w;
        uint32_t lastStops = 0;
        startWorld(w, watch, seq, rnd);
        for (int step = 0; step < STEPS_PER_SEQUENCE; ++step)
        {
            const char *broken = stepOnce(w, watch, seq, rnd, lastStops);
            if (broken != nullptr)
                return fail(f, seed, step, broken);
        }
        return true;
    }
} // namespace

void test_random_sequences_keep_every_property()
{
    Failure f{};
    for (uint32_t seed = 1; seed <= SEQUENCES; ++seed)
    {
        if (!runOne(seed, f))
        {
            char message[220];
            snprintf(message, sizeof(message), "seed %u step %d: %s", static_cast<unsigned>(f.seed), f.step, f.what);
            TEST_FAIL_MESSAGE(message);
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_random_sequences_keep_every_property);
    return UNITY_END();
}
