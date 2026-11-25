#pragma once

// Spec 030 (autoClose.ninaLostClose, owner option, off by default): how long
// N.I.N.A. has been lost without a break, for the automatic close's
// N.I.N.A.-lost close (constitution II: by default the roof never closes
// without the park confirmed). Pure: the park result's reasons and the time
// come in; whether the option may set N.I.N.A.'s park aside now goes out.

#include <cstdint>

namespace SQM
{
    namespace CloseSeq
    {
        class NinaLostWatch
        {
        public:
            struct Step
            {
                uint64_t nowMs = 0;
                // autoClose.ninaLostClose with park.source nina or
                // ninaAndSwitches (Policy::ninaLostClose), and its time.
                bool enabled = false;
                uint64_t afterMs = 300000;
                uint32_t parkReasons = 0; // Safety::ParkResult::reasons
            };
            // One step: true once N.I.N.A. has been unreachable, stale,
            // unreadable or answering with an API error without a break for
            // `afterMs`, with the option on.
            bool update(const Step &step);
            // How long it has been lost without a break (0 when it isn't).
            uint64_t lostMs() const { return lost ? lastMs - sinceMs : 0; }

        private:
            bool lost = false;
            uint64_t sinceMs = 0;
            uint64_t lastMs = 0;
        };

        // The park result's reasons say N.I.N.A. can't be read.
        bool ninaLost(uint32_t parkReasons);
    } // namespace CloseSeq
} // namespace SQM
