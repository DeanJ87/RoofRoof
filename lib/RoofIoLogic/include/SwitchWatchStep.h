#pragma once

// Spec 024 (constitution 5.0.0, Principle II): one control step of the
// stuck-switch watch (Safety::SwitchWatch). A fitted switch read open clears
// its count; with the park switches as the only park source, the roof
// reaching open (from a known state, not during a calibration run) counts an
// opening for every fitted switch. Pure: RoofControl and the demo call it.

#include <cstdint>

#include "RoofTypes.h"
#include "SwitchInput.h"
#include "SwitchWatch.h"

namespace SQM
{
    namespace RoofIo
    {
        struct WatchStep
        {
            Roof::State before = Roof::State::Unknown; // the roof's state at the last step
            Roof::State now = Roof::State::Unknown;
            bool calibrating = false;
            uint8_t fitted = 0;
            bool switchesOnly = false;
            Contact switch1 = Contact::Unreadable;
            Contact switch2 = Contact::Unreadable;
        };

        // Updates `watch`; returns the switch (1 or 2) Open is refused for, 0 when none.
        uint8_t stepSwitchWatch(Safety::SwitchWatch &watch, const WatchStep &step);
    } // namespace RoofIo
} // namespace SQM
