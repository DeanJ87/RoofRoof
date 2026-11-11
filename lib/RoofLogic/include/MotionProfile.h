#pragma once

// The speed profile of a move and the times that follow from it (research R2-R4):
// a soft start from the slow duty to the top duty, slowing by estimated distance
// near the end, and the slow duty once the estimate has arrived. Speed is taken
// as proportional to the duty.

#include <cstdint>

#include "RoofSettings.h"

namespace SQM
{
    namespace Roof
    {
        // A duty percentage as a fraction (0-1).
        float dutyFraction(uint8_t percent);

        // A fraction (clamped to 0-1) as a driver duty (0-255), rounded. Monotonic,
        // so a fraction at or below the top duty's never gives a higher duty.
        uint8_t toDuty(float fraction);

        // The duty fraction for a move from a known position, `drivenMs` after the
        // driver turned on with `remainingMm` estimated to go. Always between the
        // slow and the top duty.
        float profileFraction(const Settings &settings, uint32_t drivenMs, float remainingMm);

        // Distance covered in `elapsedMs` at `duty` (0-255).
        float advanceMm(const Settings &settings, uint8_t duty, uint32_t elapsedMs);

        // How long the profile takes to cover `distanceMm` from a standing start.
        uint32_t expectedMoveMs(const Settings &settings, float distanceMm);

        // The hard timeout for a move of `distanceMm` from a known position:
        // expected time plus the margin (constitution I).
        uint32_t moveTimeoutMs(const Settings &settings, float distanceMm);

        // The hard timeout for a move from an unknown position: the whole travel at
        // the slow duty plus the margin.
        uint32_t unknownMoveTimeoutMs(const Settings &settings);

        // The most driving allowed in total since a magnet was last confirmed (or a
        // person cleared a fault): the longest single move, a whole travel at the
        // slow duty plus the margin. Stops and restarts don't reset it.
        uint32_t driveBudgetMs(const Settings &settings);
    } // namespace Roof
} // namespace SQM
