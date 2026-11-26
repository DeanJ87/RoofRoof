#pragma once

// Spec 050 (constitution 12.1.0, II; findings F57/F59): a park request never
// stops a slew. N.I.N.A.'s Advanced API stops a slewing mount before it parks
// it, and a mount already parking reads slewing, so a second request (N.I.N.A.'s
// own park, or RoofRoof's earlier one) stopped the park part-way and the mount
// was left unparked. The safety poller sends a park only when a N.I.N.A. reading
// taken after the request says the mount isn't slewing; at park it sends
// nothing; a slew that goes on past PARK_SLEW_WAIT_MS gets the park anyway. The
// roof must still be at an end when it's sent (spec 043). Pure (constitution V):
// time and the reading come in as parameters.

#include <cstdint>

#include "NinaApi.h"
#include "ParkRule.h"

namespace SQM
{
    namespace Safety
    {
        // The longest a request waits for a slew to end. A goto and a park
        // both finish well within it; it's below the park time limit's default
        // (240 s) so the park can still be confirmed in time.
        constexpr uint32_t PARK_SLEW_WAIT_MS = 180000;
        // After RoofRoof sends a park, a reading counts only from this long
        // after it: a mount that has just been asked may not read slewing yet.
        constexpr uint32_t PARK_SENT_SETTLE_MS = 10000;

        struct ParkSendInputs
        {
            uint64_t nowMs = 0;
            Nina::MountReading reading; // N.I.N.A.'s latest (good or failed)
            bool ninaSet = true;        // an address is set
            bool roofAtEnd = false;     // RoofIo::parkRequestAllowed, as now
        };

        enum class ParkSendEvent : uint8_t
        {
            None,          // nothing waits, or still waiting (already said)
            Waiting,       // started waiting for the slew to end (once per request)
            Send,          // send the park now
            SendAtLimit,   // still slewing at PARK_SLEW_WAIT_MS: send it anyway
            AlreadyParked, // a reading says parked: nothing sent; the request is done
            Dropped,       // the roof isn't at an end: nothing sent; the request is done
        };

        class ParkSender
        {
        public:
            // A park is wanted. False when one already waits (it joins that one).
            bool request(uint64_t nowMs);
            // One pass of the poller, after its N.I.N.A. read. Send and
            // SendAtLimit count as sent (the caller sends at once).
            ParkSendEvent step(const ParkSendInputs &in);
            // New N.I.N.A. settings: a waiting request and what was seen are forgotten.
            void reset() { *this = ParkSender{}; }
            bool waiting() const { return pending; }
            // Waiting because the mount is slewing (the status's text).
            bool waitingForSlew() const { return pending && slewSeen; }

        private:
            enum class Mount : uint8_t
            {
                Slewing, // wait
                Parked,  // nothing to send
                Still,   // send
            };
            void remember(const Nina::MountReading &reading);
            uint64_t decidesFromMs() const;
            Mount judge(const Nina::MountReading &reading, uint64_t nowMs) const;
            ParkSendEvent sendNow(ParkSendEvent event, uint64_t nowMs);

            bool pending = false;
            bool slewSeen = false; // this request has waited for a slew
            uint64_t askedAtMs = 0;
            bool sentEver = false;
            uint64_t sentAtMs = 0;
            // N.I.N.A.'s last good reading: whether it said slewing, and when.
            bool lastGoodSlewing = false;
            uint64_t lastGoodAtMs = 0;
            bool lastGoodKnown = false;
        };

        // The not-parked text's addition while a request waits for a slew.
        constexpr const char *PARK_ASK_WAITING_TEXT = "waiting for the mount to stop slewing before asking N.I.N.A. to park";
    } // namespace Safety
} // namespace SQM
