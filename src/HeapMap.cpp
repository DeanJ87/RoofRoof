#include "HeapMap.h"

#include "HeapCensus.h"
#include "HeapTrend.h"
#include "HeapReserve.h"
#include "Logger.h"
#include "NetState.h"
#include "NetworkReturn.h"
#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <new>

namespace SQM
{
    namespace HeapMap
    {
        namespace
        {
            constexpr size_t MAX_TASKS = 28;
            std::atomic<bool> running{false};

            // Under the heap's lock: counts only, never allocates or logs.
            bool walkBlock(walker_heap_into_t heap, walker_block_info_t block, void *user)
            {
                auto *census = static_cast<HeapSafe::HeapCensus *>(user);
                return census->add(
                    static_cast<uintptr_t>(heap.start),
                    static_cast<uintptr_t>(heap.end),
                    reinterpret_cast<uintptr_t>(block.ptr),
                    block.size,
                    block.used);
            }

            // Working memory, taken before the walk (nothing is allocated during it).
            struct Work
            {
                HeapSafe::HeapCensus census;
                ConnectionBudget::NetworkReturn returns; // spec 048: copied before the walk
                TaskStatus_t status[MAX_TASKS];
                HeapSafe::TaskStack tasks[MAX_TASKS];
                size_t taskCount = 0;
            };

            void readTasks(Work &work)
            {
                const UBaseType_t count = uxTaskGetSystemState(work.status, MAX_TASKS, nullptr);
                work.taskCount = count;
                for (size_t i = 0; i < work.taskCount; ++i)
                {
                    work.tasks[i].name = work.status[i].pcTaskName;
                    work.tasks[i].base = reinterpret_cast<uintptr_t>(work.status[i].pxStackBase);
                    work.tasks[i].leastLeft = work.status[i].usStackHighWaterMark; // bytes on ESP-IDF
                }
                work.census.watch(work.tasks, work.taskCount);
            }

            void summary(HeapSafe::ChunkText &out)
            {
                multi_heap_info_t info = {};
                heap_caps_get_info(&info, MALLOC_CAP_8BIT);
                char text[200];
                const int n = snprintf(
                    text,
                    sizeof(text),
                    "Heap (8-bit): free %u B in %u blocks, largest %u B, lowest since start %u B; "
                    "in use %u B in %u blocks; reserve %s, spent %u times\n",
                    static_cast<unsigned>(info.total_free_bytes),
                    static_cast<unsigned>(info.free_blocks),
                    static_cast<unsigned>(info.largest_free_block),
                    static_cast<unsigned>(info.minimum_free_bytes),
                    static_cast<unsigned>(info.total_allocated_bytes),
                    static_cast<unsigned>(info.allocated_blocks),
                    HeapSafe::reserve().held() ? "held" : "spent",
                    static_cast<unsigned>(HeapSafe::reserve().spent()));
                if (n > 0)
                    out.append(text, static_cast<size_t>(n) < sizeof(text) ? static_cast<size_t>(n) : sizeof(text) - 1);
            }

            // Spec 045: the last hour, a minute at a time, newest first.
            void writeTrend(HeapSafe::ChunkText &out, const ConnectionBudget::NetworkReturn &returns)
            {
                const HeapSafe::HeapTrend &trend = HeapSafe::trend();
                char text[160];
                int n = snprintf(
                    text,
                    sizeof(text),
                    "Trend (lowest largest block / lowest free, per minute, newest first; 15 min %u B, 60 min %u B):\n",
                    static_cast<unsigned>(trend.lowestLargest(15)),
                    static_cast<unsigned>(trend.lowestLargest(60)));
                if (n > 0)
                    out.append(text, static_cast<size_t>(n) < sizeof(text) ? static_cast<size_t>(n) : sizeof(text) - 1);
                for (size_t i = 0; i < trend.size(); ++i)
                {
                    const HeapSafe::HeapTrend::Minute &m = trend.at(i);
                    // Spec 048: the minutes a network return happened in.
                    n = snprintf(
                        text,
                        sizeof(text),
                        "  minute %5u  largest %6u B  free %6u B%s\n",
                        static_cast<unsigned>(m.index),
                        static_cast<unsigned>(m.lowestLargest),
                        static_cast<unsigned>(m.lowestFree),
                        ConnectionBudget::returnedInMinute(returns, m.index) ? "  <- network return" : "");
                    if (n > 0)
                        out.append(text, static_cast<size_t>(n) < sizeof(text) ? static_cast<size_t>(n) : sizeof(text) - 1);
                }
            }

            // Spec 048: each network return kept, with what followed it.
            void writeReturns(HeapSafe::ChunkText &out, const ConnectionBudget::NetworkReturn &returns)
            {
                char text[320];
                int n = snprintf(
                    text,
                    sizeof(text),
                    "Network returns (newest first; %u since start; the heap's lows over the %u s after each):\n",
                    static_cast<unsigned>(returns.returns()),
                    static_cast<unsigned>(ConnectionBudget::RETURN_FOLLOW_MS / 1000));
                if (n > 0)
                    out.append(text, static_cast<size_t>(n) < sizeof(text) ? static_cast<size_t>(n) : sizeof(text) - 1);
                const uint32_t now = millis();
                for (size_t i = 0; i < returns.size(); ++i)
                {
                    const size_t length = ConnectionBudget::describe(returns.at(i), now, text, sizeof(text) - 1);
                    text[length] = '\n';
                    out.append(text, length + 1);
                }
            }
        } // namespace

        void upkeep()
        {
            constexpr uint32_t TREND_EVERY_MS = 1000;
            constexpr uint32_t RETURN_SAMPLE_MS = 100;
            static uint32_t spentSeen = 0;
            static uint32_t lastTrendMs = 0;
            HeapSafe::HeapReserve &reserve = HeapSafe::reserve();
            if (reserve.spent() != spentSeen)
            {
                spentSeen = reserve.spent();
                Logger::warn(
                    "Heap", "Out of memory: the emergency reserve was used (%u times since start)", static_cast<unsigned>(spentSeen));
            }
            const uint32_t largest = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            if (!reserve.held() && HeapSafe::HeapReserve::refillFits(largest))
                reserve.refill();
            // Spec 048: ten times a second while a network return is followed,
            // so its short lows are seen; into its record and the trend.
            const bool following = NetState::phase() != ConnectionBudget::ReturnPhase::Settled;
            const uint32_t now = millis();
            if (now - lastTrendMs >= (following ? RETURN_SAMPLE_MS : TREND_EVERY_MS))
            {
                lastTrendMs = now;
                const uint32_t freeBytes = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
                HeapSafe::trend().sample(now, largest, freeBytes);
                if (following)
                    NetState::noteHeap(largest, freeBytes);
            }
        }

        bool text(HeapSafe::ChunkText &out)
        {
            bool expected = false;
            if (!running.compare_exchange_strong(expected, true))
                return false;
            std::unique_ptr<Work> work(new (std::nothrow) Work());
            if (!work)
            {
                running.store(false);
                return false;
            }
            readTasks(*work);
            NetState::copyReturns(work->returns);
            summary(out);
            heap_caps_walk(MALLOC_CAP_8BIT, walkBlock, &work->census);
            work->census.write(out);
            writeTrend(out, work->returns);
            writeReturns(out, work->returns);
            running.store(false);
            return true;
        }
    } // namespace HeapMap
} // namespace SQM
