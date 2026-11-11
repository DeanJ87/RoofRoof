// The close-on-unsafe sequence's safety properties under random use (spec 005
// SC-002; constitution I-III). Thousands of seeded sequences of weather
// changes (steady, flapping), park results (confirmed, lost, flickering,
// N.I.N.A. missing), a person's stops, closes, opens and fault clears, faults
// and interlock trips mid-close, waiting closes, refusals and irregular step
// timing drive a small roof model. After every step an independent observer
// checks:
//
//   P1  never an open: only none, a park request, a close or a stop;
//   P2  a close only while not safe, with the park said parked for the last
//       4 s, the roof movable (not closed, closing, unknown or in error) and
//       not stood down by a person;
//   P3  two park requests never less than 30 s apart, and none while safe;
//   P4  after a person's stop during the sequence, no park request, close or
//       stop until the roof reads closed or the verdict has been safe for
//       10 minutes without a break;
//   P5  nothing at all while the roof is unknown or in error;
//   P6  an attempt never waits for the park longer than its time limit
//       without raising "mount not parked";
//   P7  each critical event alerts at most once per episode;
//   P8  a close in motion never outlasts its own timeout plus 30 s and the
//       step that sees it; a stop only ends a close.
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
        bool closingRun = false;
        uint64_t closingSinceMs = 0;
        uint8_t alertedThisEpisode = 0;
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
        if (!in.parked)
            watch.parkedRun = false;
        else if (!watch.parkedRun)
        {
            watch.parkedRun = true;
            watch.parkedSinceMs = in.nowMs;
        }
        if (!in.weatherSafe)
            watch.safeRun = false;
        else if (!watch.safeRun)
        {
            watch.safeRun = true;
            watch.safeSinceMs = in.nowMs;
        }
        if (in.roof == State::Closed || (watch.safeRun && in.nowMs - watch.safeSinceMs >= REARM_SAFE_MS))
            watch.held = false;
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
        if (!watch.parkedRun || in.nowMs - watch.parkedSinceMs < PARK_SETTLE_MS)
            return "P2 close without 4 s of park";
        if (!movable(in.roof) || in.closePending)
            return "P2 close of a roof that isn't movable";
        return nullptr;
    }

    // P3: park requests spaced and only while not safe.
    const char *checkRequest(const Inputs &in, const Watch &watch)
    {
        if (in.weatherSafe)
            return "P3 park request while safe";
        if (watch.anyRequest && in.nowMs - watch.lastRequestMs < PARK_REQUEST_GAP_MS)
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
        if (seen.after == Phase::WaitPark && watch.attemptOn && in.nowMs - watch.attemptSinceMs > watch.attemptLimitMs)
            return "P6 waited past the park limit without failing the attempt";
        return nullptr;
    }

    // P7: each critical event alerts once per episode.
    const char *checkAlert(const Seen &seen, Watch &watch)
    {
        if (seen.decision.alert)
        {
            const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(seen.decision.event));
            if ((watch.alertedThisEpisode & bit) != 0)
                return "P7 a critical event alerted twice in one episode";
            watch.alertedThisEpisode = static_cast<uint8_t>(watch.alertedThisEpisode | bit);
        }
        if (seen.after == Phase::Idle)
            watch.alertedThisEpisode = 0;
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
        if (in.nowMs - watch.closingSinceMs > in.moveTimeoutMs + CLOSE_GUARD_SLACK_MS + 3100)
            return "P8 a close outlasted its guard";
        return nullptr;
    }

    // P6-P8 and the observer's bookkeeping after a decision.
    const char *checkAfter(const Seen &seen, const Inputs &in, Watch &watch)
    {
        const char *broken = checkAttempt(seen, in, watch);
        if (broken == nullptr)
            broken = checkAlert(seen, watch);
        if (broken == nullptr)
            broken = checkGuard(seen, in, watch);
        return broken;
    }

    bool runOne(uint32_t seed, Failure &f)
    {
        Random rnd(seed);
        Sequence seq;
        Watch watch;
        if (rnd.chance(300))
        {
            watch.attemptLimitMs = 30000 + rnd.below(240000);
            seq.setParkTimeoutMs(watch.attemptLimitMs);
        }
        World w;
        uint32_t lastStops = 0;
        w.in.nowMs = rnd.below(100000);
        w.in.weatherSafe = rnd.chance(500);
        const State starts[] = {State::Open, State::Closed, State::Unknown, State::Stopped, State::Error};
        w.in.roof = starts[rnd.below(5)];
        w.in.parked = rnd.chance(300);
        w.in.ninaConfigured = !rnd.chance(50);
        for (int step = 0; step < STEPS_PER_SEQUENCE; ++step)
        {
            w.in.nowMs += rnd.chance(20) ? 30000 + rnd.below(120000) : 10 + rnd.below(1500);
            worldChanges(w, rnd);
            observeInputs(watch, w.in);
            const Phase before = seq.phase();
            // A person's stop seen while the sequence runs stands it down (P4),
            // unless the roof reads closed, which ends it.
            const bool stopped = w.in.personStops != lastStops && running(before) && w.in.roof != State::Closed;
            lastStops = w.in.personStops;
            const Decision d = seq.step(w.in);
            if (stopped && d.event != Event::StoppedByPerson)
                return fail(f, seed, step, "P4 a person's stop didn't stand the sequence down");
            if (stopped)
                watch.held = true;
            Seen seen{d, before, seq.phase()};
            const char *broken = checkAction(seen, w.in, watch);
            if (broken == nullptr)
                broken = checkAfter(seen, w.in, watch);
            if (broken != nullptr)
                return fail(f, seed, step, broken);
            Decision answer;
            carryOut(w, d.action, seq, rnd, answer);
            broken = checkAfter(Seen{answer, Phase::Closing, seq.phase()}, w.in, watch);
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
