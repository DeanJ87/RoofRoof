#pragma once

// Spec 047: when the safety poller reads N.I.N.A. and SQMeter, and how long an
// SQMeter reply may take. They share one task (constitution VI: one outgoing
// HTTP task, one TLS session), so a slow weather reply must never leave the
// park reading past its freshness (NINA_FRESH_MS): N.I.N.A. is read just
// before each weather poll unless it was read moments ago, and a weather poll
// can take at most SQMETER_TOTAL_MS. While the roof moves the weather is
// polled faster, so rain during an open is seen sooner. Pure (constitution V).

#include <cstdint>

#include "ParkRule.h"

namespace SQM
{
    namespace Safety
    {
        // How often each source is polled.
        constexpr uint32_t NINA_POLL_MS = 2000;           // research D8: five polls per freshness limit
        constexpr uint32_t SQMETER_POLL_MS = 5000;        // the roof at rest
        constexpr uint32_t SQMETER_MOVING_POLL_MS = 1500; // the roof moving (findings F45/F46)

        // One HTTP request's bounds: connecting, then the wait for the reply's
        // headers, and the whole request (the body is read until then).
        struct HttpLimits
        {
            uint32_t connectMs;
            uint32_t replyMs;
            uint32_t totalMs;
        };
        // SQMeter's GET /api/safety: 8 s in all (a reply of 5 s and more is
        // normal for a busy SQMeter, finding F45); a LAN connect is quick.
        constexpr uint32_t SQMETER_CONNECT_MS = 1500;
        constexpr uint32_t SQMETER_TOTAL_MS = 8000;
        constexpr HttpLimits SQMETER_LIMITS{SQMETER_CONNECT_MS, SQMETER_TOTAL_MS - SQMETER_CONNECT_MS, SQMETER_TOTAL_MS};
        // N.I.N.A. (spec 004 FR-010, FR-018): 3 s to connect, 3 s for the headers, 6 s in all.
        constexpr HttpLimits NINA_LIMITS{3000, 3000, 6000};

        // N.I.N.A. is read before a weather poll unless its last read ended
        // this recently: the reading then stays within NINA_FRESH_MS through
        // the longest weather poll and the N.I.N.A. read after it. Exact for a
        // plain http SQMeter; an https one may add the wait for the one TLS
        // session (SafetyPoller::TLS_POLL_WAIT_MS) and its handshake (3 s)
        // before SQMETER_TOTAL_MS starts to count.
        constexpr uint32_t NINA_BEFORE_WEATHER_MS = 1000;
        static_assert(NINA_BEFORE_WEATHER_MS + SQMETER_TOTAL_MS < NINA_FRESH_MS, "a weather poll can't starve the park reading");

        // What to poll in this pass of the poller (N.I.N.A. first).
        struct PollPass
        {
            bool nina = false;
            bool sqmeter = false;
        };

        // The poller task's own; times are the poller's clock (ms).
        class PollSchedule
        {
        public:
            // `moving`: the roof is moving (the weather's faster interval).
            PollPass plan(uint64_t nowMs, bool ninaOn, bool sqmeterOn, bool moving) const;
            // A N.I.N.A. read ran from `startedMs` to `endedMs`.
            void ninaRead(uint64_t startedMs, uint64_t endedMs);
            // A weather poll started at `startedMs`.
            void sqmeterPolled(uint64_t startedMs);
            // New settings: that source is polled at once.
            void ninaChanged() { ninaEver = false; }
            void sqmeterChanged() { sqmeterEver = false; }
            // When the weather poll is next due (spec 020's site reads keep
            // clear of it); `nowMs` when it is due now.
            uint64_t sqmeterNextMs(uint64_t nowMs, bool moving) const;

        private:
            bool ninaEver = false;
            uint64_t ninaStartMs = 0;
            uint64_t ninaEndMs = 0;
            bool sqmeterEver = false;
            uint64_t sqmeterStartMs = 0;
        };
    } // namespace Safety
} // namespace SQM
