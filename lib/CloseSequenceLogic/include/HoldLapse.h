#pragma once

// Spec 044 (owner decision): a person's Stop of the automatic close may lapse
// by itself. autoClose.personStopLapse "never" (the default) keeps spec 030's
// stand-down: it holds while unsafe until the roof is closed, an Open moves
// it, a person releases it or the weather has been safe for the re-arm spell.
// "rain" lets it lapse once it has rained (or the rain sensor has failed) for
// autoClose.stopHoldMinutes; "always" once any unsafe weather has lasted that
// long. The time is added up over the spells, as the weather hold adds up
// (WeatherHold.h), and forgotten after a break as long as the hold, so a
// flickering rain sensor still gets there; a new Stop starts it again. Pure:
// time and inputs come in, whether the hold has lapsed goes out. The close that
// follows goes through every gate as any other (constitution II and III).

#include <cstdint>

#include "WeatherHold.h"

namespace SQM
{
    namespace CloseSeq
    {
        // What "rain" counts (Inputs::raining): the reasons that skip the weather
        // hold (fix 042): rain or a failed rain sensor, from any source, even a stale one.
        constexpr bool rainForLapse(uint32_t weatherReasons)
        {
            return skipsHold(weatherReasons);
        }

        class HoldLapse
        {
        public:
            struct Step
            {
                uint64_t nowMs = 0;
                bool held = false;    // a person's stand-down (Phase::HeldByPerson) as the step begins
                bool newStop = false; // a person's stop on this step: the count starts again
                bool counts = false;  // the weather the setting lapses for (rain, or any unsafe) holds now
                uint64_t lapseMs = 0; // autoClose.stopHoldMinutes; 0: never lapses
            };

            // One step: whether the hold has lapsed (the count has reached lapseMs
            // while the weather it counts still holds).
            bool update(const Step &step);
            // For the status: the time left before it lapses while it counts
            // down (the last step held and counted), else NOT_LAPSING.
            uint64_t leftMs() const;

            static constexpr uint64_t NOT_LAPSING = UINT64_MAX;
            // The most one step adds to a count (a late step can't lapse it alone).
            static constexpr uint64_t MAX_STEP_MS = 1000;

        private:
            void reset();

            bool wasHeld = false; // the last step was held (its time counts for this one)
            uint64_t lastMs = 0;
            uint64_t countMs = 0; // the weather it counts, added up since the stop
            uint64_t breakMs = 0; // the current break in it
            uint64_t lapseMs = 0;
            bool counting = false; // the last step held and counted
        };
    } // namespace CloseSeq
} // namespace SQM
