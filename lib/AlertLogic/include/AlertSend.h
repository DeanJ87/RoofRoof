#pragma once

// Spec 053 (findings F55/F56): how the alert task's HTTPS deliveries wait.
//
// The main loop hands each HTTPS delivery to the alert task through a
// SendQueue: fixed room, nothing allocated, never waits, at most N steps (a
// constant), so handing one over costs the loop the same whatever the alert
// task is doing (a TLS handshake that takes seconds, a wait for memory). When
// full, the oldest non-critical delivery makes room; a critical one is only
// pushed out by a newer critical one (the outbox, spec 030, sends it again).
// The alert task takes a critical one first, else the oldest.
//
// Before it starts a TLS session the alert task asks a SendGate: while the
// roof moves it waits (checked every second), and while the heap is short of
// a session's needs (HeapSafe::tlsFits) it waits with a back-off (2 s doubling
// to 30 s). After 10 minutes of waiting it gives up: the channel is recorded
// as skipped for now, which the outbox treats as a failure to send again.
// Pure: the caller gives the clock, the roof and the heap.

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace Alerts
    {
        template <typename T, size_t N> class SendQueue
        {
        public:
            static_assert(N > 0, "a queue holds at least one delivery");
            static constexpr size_t CAPACITY = N;

            struct Push
            {
                bool accepted = false; // `item` is queued
                bool dropped = false;  // `victim` left the queue (or is `item` itself when not accepted)
                T victim{};
            };

            // Queues `item`. Full: replaces the oldest non-critical delivery;
            // all critical: a critical `item` replaces the oldest of them, a
            // non-critical one is refused (returned as the victim).
            Push push(const T &item, bool critical)
            {
                Push result;
                size_t slot = N;
                if (count < N)
                    slot = freeSlot();
                else
                {
                    slot = oldest(false);
                    if (slot == N && critical)
                        slot = oldest(true);
                    if (slot == N)
                    {
                        ++droppedCount;
                        result.dropped = true;
                        result.victim = item;
                        return result;
                    }
                    ++droppedCount;
                    result.dropped = true;
                    result.victim = slots[slot].item;
                    --count;
                }
                slots[slot] = Slot{item, critical, nextOrder++, true};
                ++count;
                result.accepted = true;
                return result;
            }

            // The next delivery: the oldest critical one, else the oldest.
            bool pop(T &out)
            {
                size_t slot = oldest(true);
                if (slot == N)
                    slot = oldest(false);
                if (slot == N)
                    return false;
                out = slots[slot].item;
                slots[slot].used = false;
                --count;
                return true;
            }

            size_t size() const { return count; }
            bool empty() const { return count == 0; }
            // Deliveries pushed out or refused because the queue was full.
            uint32_t dropped() const { return droppedCount; }

        private:
            struct Slot
            {
                T item{};
                bool critical = false;
                uint32_t order = 0;
                bool used = false;
            };

            size_t freeSlot() const
            {
                for (size_t i = 0; i < N; ++i)
                    if (!slots[i].used)
                        return i;
                return N;
            }

            // The oldest used slot of that kind; N if none.
            size_t oldest(bool critical) const
            {
                size_t found = N;
                for (size_t i = 0; i < N; ++i)
                {
                    const Slot &s = slots[i];
                    // Wrap-safe: the order counter's distance from the oldest.
                    if (s.used && s.critical == critical && (found == N || s.order - slots[found].order > UINT32_MAX / 2))
                        found = i;
                }
                return found;
            }

            Slot slots[N];
            size_t count = 0;
            uint32_t nextOrder = 0;
            uint32_t droppedCount = 0;
        };

        // Why a TLS session may not start yet.
        enum class SendWait : uint8_t
        {
            Go,            // start it
            RoofMoving,    // the roof is moving: asked again in a second
            ShortOfMemory, // the heap is below the TLS floor: asked again after the back-off
            GiveUp,        // waited SEND_GIVE_UP_MS: skipped for now (the outbox sends it again)
        };
        const char *sendWaitText(SendWait wait);

        constexpr uint32_t SEND_MOVE_RECHECK_MS = 1000;
        constexpr uint32_t SEND_MEMORY_FIRST_MS = 2000;
        constexpr uint32_t SEND_MEMORY_MAX_MS = 30000;
        constexpr uint32_t SEND_GIVE_UP_MS = 10 * 60 * 1000;

        class SendGate
        {
        public:
            // A new delivery: its wait starts now, the back-off from the start.
            void start(uint32_t nowMs);
            // Whether its TLS session may start now.
            SendWait check(uint32_t nowMs, bool roofMoving, bool memoryFits);
            // How long to wait before asking again (after RoofMoving or ShortOfMemory).
            uint32_t retryInMs() const { return retryMs; }

            // Since start: the checks that said wait, by reason, and the give-ups.
            uint32_t memoryWaits() const { return memoryWaitCount; }
            uint32_t moveWaits() const { return moveWaitCount; }
            uint32_t gaveUp() const { return gaveUpCount; }

        private:
            uint32_t startedMs = 0;
            uint32_t retryMs = 0;
            uint32_t backoffMs = SEND_MEMORY_FIRST_MS;
            uint32_t memoryWaitCount = 0;
            uint32_t moveWaitCount = 0;
            uint32_t gaveUpCount = 0;
        };
    } // namespace Alerts
} // namespace SQM
