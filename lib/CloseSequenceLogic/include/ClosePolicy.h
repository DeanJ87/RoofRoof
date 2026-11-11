#pragma once

// The automatic close's policy (spec 014): the timings and choices specs 005
// and 013 fixed, as settings. Every default is the constant it replaces, so a
// default config behaves exactly as before; every value is clamped to a range
// that keeps constitution I-III (research R3), whatever the caller passes.

#include <cstdint>

namespace SQM
{
    struct Config;

    namespace CloseSeq
    {
        // Spec 005's defaults (research R4-R10 there), kept as the named defaults.
        // Two park requests never closer than this.
        constexpr uint64_t PARK_REQUEST_GAP_MS = 30000;
        // The park result must say parked this long without a break.
        constexpr uint64_t PARK_SETTLE_MS = 4000;
        // After a failed attempt, the wait before the next park request.
        constexpr uint64_t RETRY_AFTER_MS = 300000;
        // A stood-down sequence re-arms after this long safe without a break.
        constexpr uint64_t REARM_SAFE_MS = 600000;
        // A waiting close (spec 003 closePending) must start within this.
        constexpr uint64_t CLOSE_START_MS = 30000;
        // A moving close may take its own timeout plus this.
        constexpr uint64_t CLOSE_GUARD_SLACK_MS = 30000;
        // A critical event alerts at most this often, and is raised again this
        // often while the roof stays open after it.
        constexpr uint64_t REMIND_MS = 1800000;

        struct Policy
        {
            uint64_t parkRequestGapMs = PARK_REQUEST_GAP_MS;
            uint64_t parkSettleMs = PARK_SETTLE_MS;
            uint64_t retryAfterMs = RETRY_AFTER_MS;
            uint64_t rearmSafeMs = REARM_SAFE_MS;
            uint64_t closeStartMs = CLOSE_START_MS;
            uint64_t closeGuardSlackMs = CLOSE_GUARD_SLACK_MS;
            uint64_t remindMs = REMIND_MS;
            // A waiting close that never starts: false halts (spec 005), true
            // calls it off and backs off as for a refused close.
            bool retryNotStarted = false;
        };

        // The ranges (research R3), in ms. The floors and ceilings keep the
        // constitution's rules: a park hold is never shorter than spec 005's 4 s
        // (two N.I.N.A. polls 2 s apart, so never a single reading, II), a
        // close-start limit always outlasts spec 010's 15 s network hold, and
        // reminders never stop (III).
        struct PolicyRange
        {
            uint64_t minMs;
            uint64_t maxMs;
        };
        constexpr PolicyRange PARK_REQUEST_GAP_RANGE{10000, 120000};
        constexpr PolicyRange PARK_SETTLE_RANGE{4000, 30000};
        constexpr PolicyRange RETRY_AFTER_RANGE{60000, 1800000};
        constexpr PolicyRange REARM_SAFE_RANGE{300000, 3600000};
        constexpr PolicyRange CLOSE_START_RANGE{25000, 120000};
        // At least spec 010's 5 s margin over its 15 s network hold of a close.
        static_assert(CLOSE_START_RANGE.minMs > 15000 + 5000, "the close-start floor outlasts the network hold with margin");
        constexpr PolicyRange CLOSE_GUARD_SLACK_RANGE{10000, 120000};
        constexpr PolicyRange REMIND_RANGE{600000, 7200000};

        // `policy` with every value inside its range.
        Policy clamp(const Policy &policy);
        // The policy the config asks for (autoClose.*), clamped.
        Policy policyFrom(const Config &config);
    } // namespace CloseSeq
} // namespace SQM
