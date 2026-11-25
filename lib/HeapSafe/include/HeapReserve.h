#pragma once

// Spec 038: a small block held back for the moment the heap runs out.
//
// Without C++ exceptions, the throwing operator new aborts when malloc fails:
// a std::string, std::list node or std::function inside the web server's
// request handling restarts the device (crash record 26). The C++ library
// calls the installed new-handler before it gives up; ours frees this
// reserve, so the allocation is retried and usually succeeds. While the
// reserve is spent the memory guard answers every non-essential request
// "busy" (as if the heap were short) until the main loop can take the block
// back. A second failure before then aborts as before.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace SQM
{
    namespace HeapSafe
    {
        class HeapReserve
        {
        public:
            using Allocate = void *(*)(size_t);
            using Release = void (*)(void *);

            static constexpr size_t BYTES = 3072;
            // The loop takes the block back once the largest free block is at
            // least this (so taking it can't itself starve a reply).
            static constexpr size_t REFILL_BLOCK_BYTES = 8192;

            explicit HeapReserve(Allocate allocate = std::malloc, Release release = std::free);

            // Takes the block if it isn't held; true when held afterwards.
            bool refill();
            // Whether the loop should refill now.
            static constexpr bool refillFits(size_t largestBlock) { return largestBlock >= REFILL_BLOCK_BYTES; }
            // Frees the block (from the new-handler): true if there was one.
            bool spend();
            bool held() const { return block.load() != nullptr; }
            // Times the block was given up since start.
            uint32_t spent() const { return spentCount.load(); }

        private:
            Allocate allocator;
            Release releaser;
            std::atomic<void *> block{nullptr};
            std::atomic<uint32_t> spentCount{0};
        };

        // The firmware's reserve.
        HeapReserve &reserve();
        // Takes the reserve and installs the new-handler (once, at start).
        void armReserve();
        // The new-handler: spends the reserve, or aborts when it is gone.
        void onOutOfMemory();
    } // namespace HeapSafe
} // namespace SQM
