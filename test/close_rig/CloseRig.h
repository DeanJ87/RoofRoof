#pragma once

// A close sequence and its inputs for the native tests (spec 005), shared by
// test_close_sequence and test_close_standdown.

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "CloseSequence.h"
#include "Config.h" // for the library finder: RoofLogic (Roof::State) builds with ConfigModel

namespace CloseTest
{
    using namespace SQM;
    using namespace SQM::CloseSeq;
    using Roof::State;

    constexpr uint64_t T0 = 10000;

    // What a run of steps produced.
    struct Counts
    {
        int requests = 0;
        int closes = 0;
        int stops = 0;
        int alerts = 0;
        std::vector<Decision> events;
        std::vector<uint64_t> requestTimes;
        Decision last;
    };

    // One sequence and its inputs; starts at T0 - 100 with a safe verdict and the roof open.
    struct Rig
    {
        Sequence seq;
        Inputs in;

        Rig()
        {
            in.nowMs = T0 - 100;
            in.weatherSafe = true;
            in.roof = State::Open;
            seq.step(in);
        }
        Decision at(uint64_t nowMs)
        {
            in.nowMs = nowMs;
            return seq.step(in);
        }
        // Steps every `everyMs` after now until `untilMs` (inclusive).
        Counts runTo(uint64_t untilMs, uint64_t everyMs = 100)
        {
            Counts counts;
            while (in.nowMs + everyMs <= untilMs)
                note(counts, at(in.nowMs + everyMs));
            return counts;
        }
        static void note(Counts &counts, const Decision &d)
        {
            counts.last = d;
            if (d.action == Action::RequestPark)
            {
                counts.requests++;
                counts.requestTimes.push_back(0);
            }
            if (d.action == Action::Close)
                counts.closes++;
            if (d.action == Action::Stop)
                counts.stops++;
            if (d.alert)
                counts.alerts++;
            if (d.event != Event::None)
                counts.events.push_back(d);
        }
        // Unsafe at T0 with the roof open: the sequence starts.
        Decision startUnsafe()
        {
            in.weatherSafe = false;
            return at(T0);
        }
        // Started, then the park held for 4 s: the close is commanded at T0 + 4100.
        void startAndClose() { startAndCloseFrom(T0); }
        // The same from `startMs` (the verdict is made unsafe there).
        void startAndCloseFrom(uint64_t startMs)
        {
            in.weatherSafe = false;
            in.roof = State::Open;
            in.parked = false;
            at(startMs);
            in.parked = true;
            at(startMs + 100);
            const Decision d = at(startMs + 100 + PARK_SETTLE_MS);
            TEST_ASSERT_EQUAL(Action::Close, d.action);
            seq.closeAnswered(CloseAnswer::Started);
            in.roof = State::Closing;
            in.moveTimeoutMs = 60000;
        }
    };

    // A stood-down sequence only repeats its critical event, as an alert, every REMIND_MS.
    inline void assertOnlyReminders(const Counts &counts, Event event, size_t expected)
    {
        TEST_ASSERT_EQUAL(0, counts.requests + counts.closes + counts.stops);
        TEST_ASSERT_EQUAL(expected, counts.events.size());
        for (const Decision &d : counts.events)
        {
            TEST_ASSERT_EQUAL_STRING(eventName(event), eventName(d.event));
            TEST_ASSERT_TRUE(d.alert);
            TEST_ASSERT_TRUE(d.reminder);
        }
    }

    inline void assertEvent(const Decision &d, Event event, Cause cause)
    {
        TEST_ASSERT_EQUAL_STRING(eventName(event), eventName(d.event));
        TEST_ASSERT_EQUAL_STRING(causeName(cause), causeName(d.cause));
    }
} // namespace CloseTest
