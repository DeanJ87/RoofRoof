#pragma once

// Spec 030's weather hold (FR-001..FR-004): a blip never starts the automatic
// close. The verdict itself is never delayed (Open is refused and an opening
// roof stops at once); only the sequence's view of the weather is held. The
// hold adds up the unsafe time and lets the close start once it reaches
// autoClose.unsafeHoldSeconds; rain and a failed rain sensor start it at
// once (a rain sensor that fails may be failing because it is wet; a silent,
// stale or unreadable source is still held). Once fired it stays
// fired until the weather has been safe for the hold's length without a
// break, and that safe spell is also what forgets the time added up, so a
// flapping verdict still gets there (and starts one close, not one per blip).
// Pure: time and inputs come in, the weather as the sequence sees it goes out.

#include <cstdint>

#include "SafetyInput.h"

namespace SQM
{
    namespace CloseSeq
    {
        // autoClose.unsafeHoldSeconds' default and range, in ms.
        constexpr uint64_t UNSAFE_HOLD_MS = 60000;
        constexpr uint64_t UNSAFE_HOLD_MAX_MS = 300000;
        // The most one step adds to a count (a late step can't fire the hold alone).
        constexpr uint64_t HOLD_MAX_STEP_MS = 1000;
        // The weather reasons that skip the hold (constitution III): rain, and
        // a failed rain sensor, which may have failed because it is wet. Every
        // other reason, a missing, stale or unreadable source included, is held.
        constexpr uint32_t HOLD_SKIP_REASONS = Safety::ReasonRain | Safety::ReasonRainSensorFault;

        // Whether the verdict's reasons start the close at once.
        constexpr bool skipsHold(uint32_t weatherReasons)
        {
            return (weatherReasons & HOLD_SKIP_REASONS) != 0;
        }

        class WeatherHold
        {
        public:
            // One step's inputs.
            struct Step
            {
                uint64_t nowMs = 0;
                bool weatherSafe = false;    // the verdict is Safe, after spec 027's override
                bool atOnce = false;         // skipsHold(): rain or a failed rain sensor (any source, even a stale one)
                bool overrideActive = false; // a person's weather override is in effect (spec 027)
                uint64_t holdMs = UNSAFE_HOLD_MS;
            };

            // One step: whether the weather counts as safe for the sequence
            // (it counts as unsafe only while the hold has fired).
            bool update(const Step &step);
            // A sequence restored mid-attempt after a restart: the close was
            // already wanted, so the hold starts fired.
            void startFired();
            // For the status: the unsafe time added up, the time left before
            // it fires (NOT_HOLDING unless the last step was unsafe and it
            // hasn't fired) and whether it fired.
            uint64_t countedMs() const { return countMs; }
            uint64_t leftMs() const;
            bool fired() const { return isFired; }

            static constexpr uint64_t NOT_HOLDING = UINT64_MAX;

        private:
            void reset();

            bool stepped = false;
            uint64_t lastMs = 0;
            uint64_t countMs = 0; // unsafe time added up
            uint64_t safeMs = 0;  // the current safe spell
            uint64_t holdMs = UNSAFE_HOLD_MS;
            bool isFired = false;
            bool counting = false; // the last step was unsafe
        };
    } // namespace CloseSeq
} // namespace SQM
