#include "HeapMap.h"

#include "HeapCensus.h"
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
                    "in use %u B in %u blocks\n",
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
        } // namespace

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
            summary(out);
            heap_caps_walk(MALLOC_CAP_8BIT, walkBlock, &work->census);
            work->census.write(out);
            running.store(false);
            return true;
        }
    } // namespace HeapMap
} // namespace SQM
