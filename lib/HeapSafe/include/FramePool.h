#pragma once

// Spec 037: live-update frames from a few buffers kept for the device's
// lifetime, instead of a fresh multi-KB block per frame.
//
// A status frame is about 4.4 KB and went out every 500 ms while the roof
// moved, plus once per change and per new client, each a new block held until
// every client had sent it. Interleaved with requests' short-lived blocks
// that cut the heap into pieces too small for a request (spec 037 research).
// A buffer here is free again once no client holds it; when all are held (a
// slow client) the frame is skipped, never queued behind the others.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace SQM
{
    namespace HeapSafe
    {
        class FramePool
        {
        public:
            using Frame = std::shared_ptr<std::vector<uint8_t>>; // AsyncWebSocketSharedBuffer
            // One for the status stream, one for the diagnostics stream or a
            // new client's first frame: 10 KB of a heap with about 45 KB free.
            static constexpr size_t SLOTS = 2;
            static constexpr size_t CAPACITY = 5120; // a status document (4.4 KB) with room to grow

            // Allocates the buffers; called once at start while memory is plentiful.
            void reserve();
            // A free buffer sized to `length`, or nullptr: none free, too long,
            // or not reserved. The caller fills it and hands it to the sockets.
            Frame take(size_t length);
            // Buffers no client holds now.
            size_t freeSlots();

        private:
            std::mutex lock;
            Frame slots[SLOTS];
        };
    } // namespace HeapSafe
} // namespace SQM
