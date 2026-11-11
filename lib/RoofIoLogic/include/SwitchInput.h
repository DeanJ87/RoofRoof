#pragma once

// A park switch or the rain contact on a GPIO with its pull-up (spec 003
// FR-004, research R6): a contact to ground, so low means closed. A level
// counts once read INPUT_CONFIRM_READINGS times in a row; before that, and
// after INPUT_CHATTER_MS without such a run, the input is unreadable, which
// spec 004 treats as not parked and unsafe (constitution II, III).

#include <cstdint>

#include "ParkRule.h"
#include "SafetyInput.h"

namespace SQM
{
    namespace RoofIo
    {
        constexpr uint8_t INPUT_CONFIRM_READINGS = 3;
        constexpr uint32_t INPUT_CHATTER_MS = 1000;

        enum class Contact : uint8_t
        {
            Unreadable,
            Closed,
            Open,
        };

        class SwitchInput
        {
        public:
            // One reading per control step; returns the input's state after it.
            Contact update(bool levelLow, uint32_t nowMs);
            Contact state() const { return current; }

        private:
            bool started = false;
            bool lastLow = false;
            uint8_t run = 0;            // equal readings in a row, capped
            bool confirmed = false;     // a level has counted at least once
            bool confirmedLow = false;  // that level
            uint32_t lastConfirmMs = 0; // when a run last reached the confirm count
            Contact current = Contact::Unreadable;
        };

        // A park switch: closed = parked.
        Safety::SwitchReading parkReading(Contact contact);
        // The rain contact: closed = raining.
        Safety::RainContact rainReading(Contact contact);
        // "closed", "open", "unreadable".
        const char *contactName(Contact contact);
    } // namespace RoofIo
} // namespace SQM
