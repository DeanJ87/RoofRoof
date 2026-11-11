#pragma once

// Is the mount parked? (constitution II; spec 004 US2). Only when N.I.N.A.'s
// latest reading is good, fresh, connected, at park and not slewing, AND every
// fitted park switch reads closed. Disagreement between the two, or anything
// unreadable, is "not parked"; spec 005 keeps the roof open and raises a
// critical alert on it.

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
            uint8_t fittedSwitches = 0; // 0, 1 (GPIO32) or 2 (GPIO32 and GPIO33)
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
        };

        struct ParkResult
        {
            bool parked = false;
            uint32_t reasons = 0;
            bool disagree = false; // N.I.N.A. and the switches point opposite ways
            uint32_t ninaAgeMs = 0;
            uint32_t ninaMaxAgeMs = NINA_FRESH_MS;
            std::string ninaDetail; // N.I.N.A.'s failure text, if any
        };

        ParkResult evaluatePark(const ParkInputs &inputs);
        // "parked" or "not parked: N.I.N.A. stale: reading 12 s old > 10 s; park switch 1 open".
        std::string describe(const ParkResult &result);
    } // namespace Safety
} // namespace SQM
