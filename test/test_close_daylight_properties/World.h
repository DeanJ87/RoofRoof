#pragma once

// The world of the daylight property test (test_main.cpp): the random
// source, the roof and its inputs, the observer's record and how a person,
// the weather, daylight and the roof change between steps. Copied from
// test_close_properties, with daylight added.

#include <cstdio>
#include <unity.h>

#include "CloseSequence.h"
#include "Config.h" // for the library finder: RoofLogic (Roof::State) builds with ConfigModel

namespace
{
    using namespace SQM;
    using namespace SQM::CloseSeq;
    using Roof::State;

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
        uint32_t daylightDwellMs = 0; // how long daylight stays as it is
        uint64_t daylightChangedMs = 0;
    };

    // A close is wanted: the weather isn't safe, or daylight wants it (spec 013).
    bool wanted(const Inputs &in)
    {
        return !in.weatherSafe || in.daylight;
    }

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
        bool alertedEver[8][10] = {};
        uint64_t alertedAtMs[8][10] = {};
        bool halted = false;          // a close outlasted its guard
        uint64_t closeBarUntilMs = 0; // a refused or interrupted close: no close before this
        uint64_t quietUnsafeMs = 0;   // unsafe and not closed, with nothing done or raised
        uint64_t lastStepMs = 0;
        bool prevUnsafe = false;
        State lastRoof = State::Unknown;
        bool closedByMove = false;   // the roof went from closing to closed this step
        bool daylightOnly = false;   // the episode is daylight-only (the observer's own tracking)
        bool expectRearm = false;    // D1: this step must leave a stand-down, halt or back-off
        bool afterDaylight = false;  // a daylight-only episode ended (the next weather one alerts afresh)
        uint64_t heldUnsafeMs = 0;   // unsafe weather added up during a person's daylight stand-down
        bool heldPrevUnsafe = false; // the previous step's weather (for the sum)
        uint64_t heldPrevMs = 0;
        bool closeBarDaylight = false; // the close back-off came from a daylight close
        bool justRestarted = false;    // the first step after a restart raises its reminder first
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

    // Daylight comes and goes in long spells (sunrise, a mode change), sometimes quickly.
    void advanceDaylight(World &w, Random &rnd)
    {
        if (w.in.nowMs - w.daylightChangedMs < w.daylightDwellMs)
            return;
        w.in.daylight = !w.in.daylight;
        w.daylightChangedMs = w.in.nowMs;
        w.daylightDwellMs = rnd.chance(200) ? 200 + rnd.below(5000) : 5000 + rnd.below(1800000);
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
        if (rnd.chance(1) && !wanted(in) && (in.roof == State::Closed || in.roof == State::Stopped))
            in.roof = State::Opening;
        if (rnd.chance(2) && in.roof == State::Error)
            in.roof = rnd.chance(500) ? State::Unknown : State::Open; // a person cleared the fault
        if (rnd.chance(2) && in.roof == State::Unknown)
            in.roof = rnd.chance(500) ? State::Closed : State::Open; // a person moved it to a magnet
    }

    void roofRuns(World &w, Random &rnd)
    {
        Inputs &in = w.in;
        if (in.roof == State::Opening && (wanted(in) || rnd.chance(20)))
            in.roof = !wanted(in) ? State::Open : State::Stopped; // spec 003 (and 013) stops an open when a close is wanted
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
        advanceDaylight(w, rnd);
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

} // namespace
