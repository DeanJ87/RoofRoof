#include "ReplyLedger.h"

namespace SQM
{
    namespace HeapSafe
    {
        size_t ReplyLedger::open(uint32_t nowMs)
        {
            const uint32_t stamp = nowMs == 0 ? 1 : nowMs;
            for (size_t i = 0; i < SLOTS; ++i)
            {
                uint32_t expected = 0;
                if (startedAt[i].compare_exchange_strong(expected, stamp))
                    return i;
            }
            return NO_SLOT;
        }

        void ReplyLedger::close(size_t slot)
        {
            if (slot < SLOTS)
                startedAt[slot].store(0);
        }

        uint32_t ReplyLedger::sending(uint32_t nowMs) const
        {
            uint32_t count = 0;
            for (const std::atomic<uint32_t> &slot : startedAt)
            {
                const uint32_t started = slot.load();
                if (started != 0 && nowMs - started < STALE_MS)
                    ++count;
            }
            return count;
        }

        uint32_t ReplyLedger::stale(uint32_t nowMs) const
        {
            uint32_t count = 0;
            for (const std::atomic<uint32_t> &slot : startedAt)
            {
                const uint32_t started = slot.load();
                if (started != 0 && nowMs - started >= STALE_MS)
                    ++count;
            }
            return count;
        }
    } // namespace HeapSafe
} // namespace SQM
