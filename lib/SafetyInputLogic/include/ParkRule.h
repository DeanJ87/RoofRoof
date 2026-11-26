#pragma once

// Is the mount parked? (constitution II; spec 004 US2). Only when N.I.N.A.'s
// latest reading is good, fresh, connected, at park and not slewing, AND every
// fitted park switch reads closed. Disagreement between the two, or anything
// unreadable, is "not parked"; spec 005 keeps the roof open and raises a
// critical alert on it. Spec 024 (constitution 5.0.0): with the switches as
// the only source, every fitted switch closed confirms it, and N.I.N.A. counts
// only against it (a fresh reading that says the mount is slewing).

#include <cstdint>
#include <string>

#include "NinaApi.h"

namespace SQM
{
    namespace Safety
    {
        // N.I.N.A. is polled every 2 s; five polls (research D8).
        constexpr uint32_t NINA_FRESH_MS = 10000;

        enum class SwitchReading : uint8_t
        {
            Closed,
            Open,
            Unreadable,
        };

        struct ParkInputs
        {
            Nina::MountReading nina;
            uint8_t fittedSwitches = 0; // 0, 1 (park switch A) or 2 (A and B)
            bool switchesOnly = false;  // spec 024: park.source is the switches alone
            SwitchReading switch1 = SwitchReading::Unreadable;
            SwitchReading switch2 = SwitchReading::Unreadable;
            uint64_t nowMs = 0;
            uint32_t ninaMaxAgeMs = NINA_FRESH_MS;
        };

        // Why the mount doesn't count as parked (bits of ParkResult::reasons).
        enum ParkReason : uint32_t
        {
            ParkNinaNotConfigured = 1u << 0,
            ParkNinaWaiting = 1u << 1,
            ParkNinaUnreachable = 1u << 2,
            ParkNinaUnreadable = 1u << 3,
            ParkNinaApiError = 1u << 4,
            ParkNinaStale = 1u << 5,
            ParkMountNotConnected = 1u << 6,
            ParkMountNotAtPark = 1u << 7,
            ParkMountSlewing = 1u << 8,
            ParkSwitch1Open = 1u << 9,
            ParkSwitch1Unreadable = 1u << 10,
            ParkSwitch2Open = 1u << 11,
            ParkSwitch2Unreadable = 1u << 12,
            ParkSourcesDisagree = 1u << 13,
            ParkNoSwitchFitted = 1u << 14, // spec 024: switches only, none fitted
        };

        struct ParkResult
        {
            bool parked = false;
            uint32_t reasons = 0;
            bool disagree = false; // N.I.N.A. and the switches point opposite ways
            uint32_t ninaAgeMs = 0;
            uint32_t ninaMaxAgeMs = NINA_FRESH_MS;
            std::string ninaDetail; // N.I.N.A.'s failure text, if any
            // Spec 022: parked only because the test box's park bypass ("no
            // telescope") stands in for N.I.N.A. (TestBox::applyPark).
            bool bypassed = false;
            bool switchesOnly = false; // spec 024: confirmed by the switches alone
        };

        ParkResult evaluatePark(const ParkInputs &inputs);
        // "parked" or "not parked: N.I.N.A. stale: reading 12 s old > 10 s; park switch 1 open".
        std::string describe(const ParkResult &result);

        // Spec 021: N.I.N.A.'s latest good reading, kept across failed reads, and
        // the poller's address generation it came from.
        struct NinaMemory
        {
            Nina::MountReading lastGood;
            uint32_t generation = 0;
            bool known = false;
        };

        // A good reading replaces the memory; a failed read (no reply, an
        // unreadable reply, an API error) keeps it; "not configured", "not read
        // yet" and a new address generation forget it.
        void rememberNina(NinaMemory &memory, const Nina::MountReading &reading, uint32_t generation);

        // Spec 021 (constitution II, re-checked throughout the close): whether a
        // move the park interlock watches may go on, and until when.
        struct CloseHold
        {
            bool parked = false; // the park result says parked
            // Not parked only because N.I.N.A.'s latest read failed, and the
            // remembered good reading, with the switches as read now, confirms the park.
            bool riding = false;
            uint64_t freshUntilMs = 0; // the relied-on reading's takenAtMs + ninaMaxAgeMs; 0 when neither
            uint32_t ageMs = 0;        // that reading's age now
            // Spec 047 (constitution 12.0.0, II): not parked only because
            // N.I.N.A. has no fresh answer (no reply, an unreadable reply, an
            // API error, or its last reading past its freshness), and that last
            // answer, with the switches as read now, said parked. Riding
            // implies it. Unbounded in time: a move that started with the park
            // confirmed goes on to its end on it; it never starts one.
            bool unanswered = false;
        };

        // `result` is evaluatePark(inputs). A ride-through (and spec 047's
        // unanswered park) never starts a move:
        // only the park result confirms a park for a start. With the switches
        // alone (spec 024) nothing rides and nothing is bounded by N.I.N.A.
        CloseHold closeHold(const ParkInputs &inputs, const ParkResult &result, const NinaMemory &memory);
        // "riding on a parked reading 3.2 s old (limit 10 s)", or "" when not riding.
        std::string describe(const CloseHold &hold, uint32_t limitMs);
    } // namespace Safety
} // namespace SQM
