#pragma once

// Fix 041 (shakedown F35): the replies being sent, each with when it started.
//
// Spec 037 caps the built replies sent at once (MAX_REPLIES_IN_FLIGHT): past
// it a non-essential API request is answered "busy". A reply counted until it
// was destroyed, and a reply whose connection stops moving (the network
// changed under it, or the web server can't get its send buffer) is never
// destroyed: two of them answered every person's API request "busy" for good
// while Alpaca, which isn't capped, kept working. A reply still being sent
// after STALE_MS isn't putting a burst on the heap any more; it no longer
// counts against the cap (it is still counted as stale, for the log).

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace HeapSafe
    {
        class ReplyLedger
        {
        public:
            // A reply's whole life is a few hundred ms; ten seconds is stuck.
            static constexpr uint32_t STALE_MS = 10000;
            static constexpr size_t SLOTS = 8;
            static constexpr size_t NO_SLOT = SLOTS;

            // A reply started at `nowMs`: its slot, NO_SLOT (not counted) when
            // every slot is taken. Any task.
            size_t open(uint32_t nowMs);
            // The reply in `slot` is gone (NO_SLOT: nothing to do).
            void close(size_t slot);
            // Replies started within STALE_MS of `nowMs`: what the cap counts.
            uint32_t sending(uint32_t nowMs) const;
            // Replies older than that, still not gone.
            uint32_t stale(uint32_t nowMs) const;

        private:
            // 0: free; otherwise the start time (0 is stored as 1).
            std::atomic<uint32_t> startedAt[SLOTS] = {};
        };
    } // namespace HeapSafe
} // namespace SQM
