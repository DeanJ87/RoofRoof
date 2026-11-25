#include "HeapReserve.h"

#include <new>

namespace SQM
{
    namespace HeapSafe
    {
        HeapReserve::HeapReserve(Allocate allocate, Release release)
            : allocator(allocate),
              releaser(release)
        {
        }

        bool HeapReserve::refill()
        {
            if (held())
                return true;
            void *fresh = allocator(BYTES);
            if (fresh == nullptr)
                return false;
            void *expected = nullptr;
            if (!block.compare_exchange_strong(expected, fresh))
                releaser(fresh); // taken meanwhile (never, with one refiller)
            return true;
        }

        bool HeapReserve::spend()
        {
            void *held = block.exchange(nullptr);
            if (held == nullptr)
                return false;
            releaser(held);
            spentCount.fetch_add(1);
            return true;
        }

        HeapReserve &reserve()
        {
            static HeapReserve firmwareReserve;
            return firmwareReserve;
        }

        void onOutOfMemory()
        {
            // Called by operator new with malloc failed; nothing here allocates.
            if (!reserve().spend())
                std::abort(); // what operator new would have done
        }

        void armReserve()
        {
            reserve().refill();
            std::set_new_handler(onOutOfMemory);
        }
    } // namespace HeapSafe
} // namespace SQM
