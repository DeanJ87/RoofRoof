// Differential fuzz of the close sequence (spec 014 SC-002): the same seeded
// world as test/test_close_properties (weather spells and flapping, park
// results, a person's stops and moves, faults, waiting closes, refusals,
// restarts, irregular steps) plus daylight, driving whichever lib/ this file is
// compiled against. It prints one line per run: the seed and a hash of every
// decision, phase and saved record, so two builds can be compared line by
// line (tools/close-diff/run.sh). It uses only what both trees offer, so a
// tree with spec 014's policy runs its defaults.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AlertRetry.h"
#include "CloseSequence.h"

using namespace SQM;
using namespace SQM::CloseSeq;
using Roof::State;

namespace
{
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
        bool alertedEver[8][9] = {};
        uint64_t alertedAtMs[8][9] = {};
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

    struct Hash
    {
        uint64_t h = 1469598103934665603ULL;
        void add(uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
            {
                h ^= (v >> (i * 8)) & 0xff;
                h *= 1099511628211ULL;
            }
        }
        void add(const Decision &d)
        {
            add(static_cast<uint64_t>(d.action));
            add(static_cast<uint64_t>(d.event));
            add(static_cast<uint64_t>(d.cause));
            add(d.alert);
            add(d.limitMs);
            add(d.reminder);
            add(d.daylight);
            if (isCritical(d.event))
            {
                const AlertText text = alertFor(d, "why");
                for (const char c : text.title + "|" + text.message)
                    add(static_cast<uint64_t>(static_cast<unsigned char>(c)));
            }
        }
        void add(const Sequence::Saved &s)
        {
            add(static_cast<uint64_t>(s.phase));
            add(static_cast<uint64_t>(s.event));
            add(static_cast<uint64_t>(s.cause));
            add(s.unsafeMs);
            add(s.wallMs);
            add(s.limitMs);
            add(s.requested);
            add(s.requestAgeMs);
            add(s.daylight);
        }
    };

    // The alert resend schedule, fed from the same random stream.
    void stepRetry(AlertRetry &retry, Random &rnd, uint64_t nowMs, Hash &hash)
    {
        if (rnd.chance(5))
            retry.sent(nowMs);
        const Delivery deliveries[] = {Delivery::Pending, Delivery::Delivered, Delivery::Failed, Delivery::NoChannel};
        const Delivery delivery = deliveries[rnd.chance(950) ? 2 : rnd.below(4)];
        const bool due = retry.resendDue(delivery, nowMs);
        if (due)
            retry.resent(nowMs);
        hash.add(due);
        hash.add(retry.gapMs());
        hash.add(retry.active());
    }

    uint64_t runOne(uint32_t seed, int &firstStep)
    {
        Random rnd(seed);
        Sequence seq;
        World w;
        AlertRetry retry;
        uint32_t limitMs = DEFAULT_PARK_TIMEOUT_MS;
        if (rnd.chance(300))
        {
            limitMs = 30000 + rnd.below(240000);
            seq.setParkTimeoutMs(limitMs);
        }
        w.in.nowMs = rnd.below(100000);
        w.in.weatherSafe = rnd.chance(500);
        const State starts[] = {State::Open, State::Closed, State::Unknown, State::Stopped, State::Error};
        w.in.roof = starts[rnd.below(5)];
        w.in.parked = rnd.chance(300);
        w.in.ninaConfigured = !rnd.chance(50);
        Hash hash;
        firstStep = -1;
        for (int step = 0; step < STEPS_PER_SEQUENCE; ++step)
        {
            w.in.nowMs += rnd.chance(20) ? 30000 + rnd.below(120000) : 10 + rnd.below(1500);
            if (rnd.chance(3))
            {
                const Sequence::Saved saved = seq.saved();
                hash.add(saved);
                seq = Sequence();
                seq.setParkTimeoutMs(limitMs);
                seq.restore(saved);
                if (w.in.roof == State::Closing || w.in.roof == State::Opening)
                    w.in.roof = State::Unknown;
                w.in.closePending = false;
                w.in.personStops = 0;
            }
            worldChanges(w, rnd);
            if (rnd.chance(4))
                w.in.daylight = !w.in.daylight;
            w.in.roofStarted = !rnd.chance(1);
            const Decision d = seq.step(w.in);
            hash.add(d);
            hash.add(static_cast<uint64_t>(seq.phase()));
            hash.add(seq.daylightEpisode());
            Decision answer;
            carryOut(w, d.action, seq, rnd, answer);
            hash.add(answer);
            hash.add(seq.saved());
            stepRetry(retry, rnd, w.in.nowMs, hash);
        }
        (void)firstStep;
        return hash.h;
    }
} // namespace

int main(int argc, char **argv)
{
    const uint32_t runs = argc > 1 ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 1000;
    for (uint32_t seed = 1; seed <= runs; ++seed)
    {
        int first = 0;
        std::printf("%u %016llx\n", static_cast<unsigned>(seed), static_cast<unsigned long long>(runOne(seed, first)));
    }
    return 0;
}
