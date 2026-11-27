#pragma once

// Spec 051 (finding F60): is N.I.N.A. still reading the mount? When the mount's
// serial link drops, the ASCOM driver can stay "connected" on a dead port and
// N.I.N.A. answers mount/info with its last known values, Connected true and
// AtPark, Slewing, SiderealTime frozen (once for 26 h on the real rig). The
// mount's own clock, SiderealTime, advances about a second a second; a reading
// whose clock hasn't moved for MOUNT_CLOCK_STALE_MS is marked frozen: status
// Unreadable (the park is unknown, as for any reply that can't be relied on).
// No SiderealTime, no judgement: the reading is kept as it is (as before).
// Pure (constitution V): time comes in the reading.

#include <cstdint>

#include "NinaApi.h"

namespace SQM
{
    namespace Nina
    {
        // A clock that hasn't moved this long is frozen. Long enough for
        // N.I.N.A.'s own device polling (2 s by default, often 5 s) to show a
        // change several times; short enough that a cached park can't stand.
        constexpr uint32_t MOUNT_CLOCK_STALE_MS = 20000;
        // Good readings further apart than this (N.I.N.A. away, restarted)
        // start the check again: the old clock proves nothing about the new one.
        constexpr uint32_t MOUNT_CLOCK_GAP_MS = 20000;
        // A clock change more than this ahead of the time that passed (or any
        // step back) is a new clock (a restarted N.I.N.A., another mount): the
        // check starts again from it. It is live either way: its values change.
        constexpr uint32_t MOUNT_CLOCK_TOLERANCE_MS = 10000;
        // The reading's detail while frozen; the park text adds "park unknown".
        constexpr const char *MOUNT_FROZEN_TEXT = "N.I.N.A.'s mount values have stopped updating (mount connection stuck?)";

        enum class ClockChange : uint8_t
        {
            None,
            Frozen,   // just became frozen (log once)
            Live,     // was frozen; the clock moved again (log once)
            Unjudged, // was frozen; no longer judged (no reading, no clock, not connected, a gap, new settings)
        };

        class MountClockWatch
        {
        public:
            // Judges a reading just taken, in order; a frozen one is turned into
            // a failed reading (Unreadable, `frozen`, MOUNT_FROZEN_TEXT).
            ClockChange apply(MountReading &reading);
            // New N.I.N.A. settings: nothing seen is kept. Returns Unjudged if it was frozen.
            ClockChange reset();
            bool frozen() const { return isFrozen; }

        private:
            ClockChange restart(const MountReading &reading);

            bool known = false;        // a baseline is held
            double baseHours = 0.0;    // the clock when it last moved
            uint64_t baseAtMs = 0;     // when that reading was taken
            uint64_t lastGoodAtMs = 0; // the last good, judged reading (for the gap)
            bool isFrozen = false;
        };

        // The clock's forward step from `fromHours` to `toHours` (0-24 h, across
        // the 24 h wrap) in solar milliseconds; a step back reads as nearly 24 h.
        double siderealStepMs(double fromHours, double toHours);
    } // namespace Nina
} // namespace SQM
