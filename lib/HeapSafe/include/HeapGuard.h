#pragma once

// Spec 031: the memory guard's rule. When the heap is too fragmented for a
// reply, a request that isn't essential is refused ("busy, try again") before
// anything is allocated, so Alpaca and the roof's commands keep what they
// need (research R7).

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace HeapSafe
    {
        // One reply needs at most a 3 KB pool, 1 KB pieces and the web server's
        // 2.9 KB send buffer, each its own block.
        constexpr uint32_t MIN_BLOCK_BYTES = 6144;
        constexpr uint32_t MIN_FREE_BYTES = 24576;
        // Room left beside a WebSocket frame for the network's own buffers.
        constexpr uint32_t FRAME_MARGIN_BYTES = 4096;

        // The Alpaca API, the roof's commands (Stop among them) and the
        // uploads: never refused by the guard.
        bool essential(const char *path, bool isPost);
        // Whether a request may run with this heap.
        bool admit(const char *path, bool isPost, uint32_t largestBlock, uint32_t freeHeap);
        // Whether a WebSocket frame of `length` bytes may be allocated.
        bool frameFits(size_t length, uint32_t largestBlock);
    } // namespace HeapSafe
} // namespace SQM
