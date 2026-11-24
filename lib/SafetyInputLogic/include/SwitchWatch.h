#pragma once

// Spec 024 (constitution 5.0.0, Principle II): with the park switches as the
// only park source, a switch shorted to ground or stuck closed reads "parked"
// for ever. A switch that hasn't once read open over STUCK_OPENINGS roof
// openings in a row is suspect, and Open is refused until it has been seen
// open. Refusing Open (never the close) means the check bites while the roof
// is closed. Pure: the control task counts and stores (src/RoofControl.cpp).

#include <cstdint>

#include "ParkRule.h"

namespace SQM
{
    namespace Safety
    {
        constexpr uint8_t STUCK_OPENINGS = 3;

        // Roof openings since each switch last read open (saturates at 255).
        struct SwitchWatch
        {
            uint8_t quiet[2] = {0, 0};
            bool operator==(const SwitchWatch &other) const { return quiet[0] == other.quiet[0] && quiet[1] == other.quiet[1]; }
            bool operator!=(const SwitchWatch &other) const { return !(*this == other); }
        };

        // A fitted switch reading open clears its count. True when the watch changed.
        bool watchReadings(SwitchWatch &watch, uint8_t fitted, SwitchReading switch1, SwitchReading switch2);
        // The roof reached open: each fitted switch's count goes up. True when changed.
        bool countOpening(SwitchWatch &watch, uint8_t fitted);
        // The switch (1 or 2) whose count refuses Open, 0 when none. Only with
        // the switches as the only park source.
        uint8_t stuckSwitch(const SwitchWatch &watch, uint8_t fitted, bool switchesOnly);
    } // namespace Safety
} // namespace SQM
