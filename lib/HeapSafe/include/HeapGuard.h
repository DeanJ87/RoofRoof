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
        // Spec 037: below this largest free block a new connection is refused
        // before the web server allocates its request (about 1 KB with its
        // shared pointer); past that point it couldn't be answered anyway.
        constexpr uint32_t MIN_ACCEPT_BLOCK_BYTES = 2048;
        // Room left beside a WebSocket frame for the network's own buffers.
        constexpr uint32_t FRAME_MARGIN_BYTES = 4096;

        // The Alpaca API, the roof's commands (Stop, Close and clear-fault
        // among them), a restart and the uploads: never refused by the guard.
        bool essential(const char *path, bool isPost);
        // Whether a request may run with this heap.
        bool admit(const char *path, bool isPost, uint32_t largestBlock, uint32_t freeHeap);
        // Spec 035: whether the main loop may hand a queued alert to the
        // dispatcher now (its record, the MQTT text and the HTTPS job: a few
        // small blocks). Below it, the alert waits for a later step.
        bool alertFits(uint32_t largestBlock, uint32_t freeHeap);
        // Spec 053 (findings F55/F56): the heap an HTTPS alert needs before its
        // TLS session may start. A session (mbedTLS, dynamic buffers, 16 KB in
        // / 4 KB out) takes about 25-40 KB in pieces, the largest the 16.4 KB
        // record buffer (spec 038). Started on a short heap it took the device
        // to a 5 KB largest block and starved everything else. So: a 44 KiB
        // (45 KB) free block, so the session comes out of one region and leaves
        // the scattered small blocks to the web server and the network; and
        // 56 KiB free, so 16 KB is left with the session at its largest (the
        // spec 037 soak's floor). Below either, the send waits (never started).
        constexpr uint32_t TLS_MIN_LARGEST_BLOCK_BYTES = 44 * 1024;
        constexpr uint32_t TLS_MIN_FREE_BYTES = 56 * 1024;
        // Whether a TLS session may start with this heap (8-bit heap figures).
        bool tlsFits(uint32_t largestBlock, uint32_t freeHeap);
        // Spec 037: built replies (about 9 KB each with the server's buffers)
        // being sent at once. Past this a non-essential API request is
        // answered "busy": admitting several at the same free-heap reading
        // took the heap to 2 KB under a 2 Hz status poll on the bench.
        constexpr uint32_t MAX_REPLIES_IN_FLIGHT = 2;
        // Whether one more reply may be built; pages, files and essential
        // requests are never held back by it.
        bool replySlotFree(const char *path, bool isPost, uint32_t repliesInFlight);
        // Spec 048: the same with a cap of `limit` (one in a network return's window).
        bool replySlotFree(const char *path, bool isPost, uint32_t repliesInFlight, uint32_t limit);
        // Whether a WebSocket frame of `length` bytes may be allocated.
        bool frameFits(size_t length, uint32_t largestBlock);
        // Whether a new connection may be accepted.
        constexpr bool acceptFits(uint32_t largestBlock)
        {
            return largestBlock >= MIN_ACCEPT_BLOCK_BYTES;
        }
    } // namespace HeapSafe
} // namespace SQM
