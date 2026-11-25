#pragma once

// Spec 038: a census of the byte-addressable heap, for GET /api/diag/heap.
//
// The firmware walks the heap block by block (ESP-IDF heap_caps_walk, under
// the heap's lock) and hands each block to add(); nothing here allocates, so
// it is safe inside that walk. The text then says where the memory went: each
// region's free space and largest free block, how many blocks of each size
// are in use or free, the largest blocks in use (a task's stack is named),
// and each task's stack with the least it has had left.

#include <cstddef>
#include <cstdint>

#include "ChunkText.h"

namespace SQM
{
    namespace HeapSafe
    {
        // One task's stack, as FreeRTOS reports it.
        struct TaskStack
        {
            const char *name = "";
            uintptr_t base = 0;    // lowest address (where its heap block starts)
            size_t leastLeft = 0;  // bytes never used since the task started
            size_t blockBytes = 0; // its heap block, found by the walk (0: not on this heap)
        };

        class HeapCensus
        {
        public:
            static constexpr size_t MAX_REGIONS = 8;
            static constexpr size_t BUCKETS = 8; // up to 32, 128, 512 B, 1, 2, 4, 8 KB, larger
            static constexpr size_t TOP_USED = 16;

            struct Region
            {
                uintptr_t start = 0;
                size_t bytes = 0;
                size_t freeBytes = 0;
                size_t largestFree = 0;
                uint32_t usedBlocks = 0;
                uint32_t freeBlocks = 0;
            };
            struct Bucket
            {
                uint32_t count = 0;
                size_t bytes = 0;
            };
            struct Block
            {
                uintptr_t address = 0;
                size_t bytes = 0;
            };

            // The tasks whose stacks the walk should name (kept, not copied;
            // their blockBytes are filled in by add()).
            void watch(TaskStack *tasks, size_t taskCount);

            // One block of the region at [regionStart, regionEnd). False once
            // the regions are full (the walk may stop).
            bool add(uintptr_t regionStart, uintptr_t regionEnd, uintptr_t address, size_t bytes, bool used);

            static size_t bucketOf(size_t bytes);
            size_t regionCount() const { return regions; }
            const Region &region(size_t index) const { return region_[index]; }
            const Bucket &usedBucket(size_t index) const { return used_[index]; }
            const Bucket &freeBucket(size_t index) const { return free_[index]; }
            size_t topCount() const { return tops; }
            const Block &top(size_t index) const { return top_[index]; }
            // The watched task whose stack starts inside the block at
            // `address` (allowing for the allocator's header), or nullptr.
            TaskStack *stackAt(uintptr_t address) const;

            // The whole census as text.
            void write(ChunkText &out) const;

        private:
            Region *regionFor(uintptr_t start, uintptr_t end);
            void keepIfLarge(uintptr_t address, size_t bytes);
            void writeRegions(ChunkText &out) const;
            void writeBuckets(ChunkText &out) const;
            void writeTops(ChunkText &out) const;
            void writeTasks(ChunkText &out) const;

            Region region_[MAX_REGIONS];
            size_t regions = 0;
            Bucket used_[BUCKETS];
            Bucket free_[BUCKETS];
            Block top_[TOP_USED]; // largest first
            size_t tops = 0;
            TaskStack *tasks_ = nullptr;
            size_t taskCount_ = 0;
        };
    } // namespace HeapSafe
} // namespace SQM
