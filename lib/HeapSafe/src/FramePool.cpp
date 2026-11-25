#include "FramePool.h"

namespace SQM
{
    namespace HeapSafe
    {
        void FramePool::reserve()
        {
            const std::lock_guard<std::mutex> guard(lock);
            for (Frame &slot : slots)
            {
                if (slot)
                    continue;
                slot = std::make_shared<std::vector<uint8_t>>(); // heap-audit: at start, once
                slot->reserve(CAPACITY);
            }
        }

        FramePool::Frame FramePool::take(size_t length)
        {
            const std::lock_guard<std::mutex> guard(lock);
            if (length == 0 || length > CAPACITY)
                return nullptr;
            for (Frame &slot : slots)
            {
                // Only this pool holds it: no client is sending it any more.
                // Copies are only made from here, under this lock.
                if (slot && slot.use_count() == 1)
                {
                    slot->resize(length); // within its capacity: no allocation
                    return slot;
                }
            }
            return nullptr;
        }

        size_t FramePool::freeSlots()
        {
            const std::lock_guard<std::mutex> guard(lock);
            size_t n = 0;
            for (const Frame &slot : slots)
                n += slot && slot.use_count() == 1 ? 1 : 0;
            return n;
        }
    } // namespace HeapSafe
} // namespace SQM
