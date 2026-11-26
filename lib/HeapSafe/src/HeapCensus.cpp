#include "HeapCensus.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace SQM
{
    namespace HeapSafe
    {
        namespace
        {
            constexpr size_t BUCKET_LIMITS[HeapCensus::BUCKETS - 1] = {32, 128, 512, 1024, 2048, 4096, 8192};
            constexpr const char *BUCKET_NAMES[HeapCensus::BUCKETS] = {"<=32", "<=128", "<=512", "<=1K", "<=2K", "<=4K", "<=8K", ">8K"};
            // A stack's block starts a little before the stack itself (the
            // allocator's header and, with heap poisoning, its canary).
            constexpr uintptr_t BLOCK_HEADER_SLACK = 32;

            void line(ChunkText &out, const char *format, ...) __attribute__((format(printf, 2, 3)));
            void line(ChunkText &out, const char *format, ...)
            {
                char text[128];
                va_list args;
                va_start(args, format);
                const int n = vsnprintf(text, sizeof(text), format, args);
                va_end(args);
                if (n > 0)
                    out.append(text, static_cast<size_t>(n) < sizeof(text) ? static_cast<size_t>(n) : sizeof(text) - 1);
                out += '\n';
            }
        } // namespace

        size_t HeapCensus::bucketOf(size_t bytes)
        {
            for (size_t i = 0; i < BUCKETS - 1; ++i)
                if (bytes <= BUCKET_LIMITS[i])
                    return i;
            return BUCKETS - 1;
        }

        HeapCensus::Region *HeapCensus::regionFor(uintptr_t start, uintptr_t end)
        {
            for (size_t i = 0; i < regions; ++i)
                if (regionList[i].start == start)
                    return &regionList[i];
            if (regions == MAX_REGIONS)
                return nullptr;
            Region &fresh = regionList[regions++];
            fresh.start = start;
            fresh.bytes = end > start ? end - start : 0;
            return &fresh;
        }

        void HeapCensus::keepIfLarge(uintptr_t address, size_t bytes)
        {
            if (tops == TOP_USED && bytes <= topBlocks[TOP_USED - 1].bytes)
                return;
            size_t at = tops < TOP_USED ? tops++ : TOP_USED - 1;
            while (at > 0 && topBlocks[at - 1].bytes < bytes)
            {
                topBlocks[at] = topBlocks[at - 1];
                --at;
            }
            topBlocks[at] = {address, bytes};
        }

        bool HeapCensus::add(uintptr_t regionStart, uintptr_t regionEnd, uintptr_t address, size_t bytes, bool used)
        {
            Region *region = regionFor(regionStart, regionEnd);
            if (region == nullptr)
                return false;
            noteForMap(regionStart, address, bytes, used);
            Bucket &bucket = (used ? usedBuckets : freeBuckets)[bucketOf(bytes)];
            ++bucket.count;
            bucket.bytes += bytes;
            if (used)
            {
                ++region->usedBlocks;
                keepIfLarge(address, bytes);
                TaskStack *stack = stackAt(address);
                if (stack != nullptr)
                    stack->blockBytes = bytes;
                return true;
            }
            ++region->freeBlocks;
            region->freeBytes += bytes;
            if (bytes > region->largestFree)
                region->largestFree = bytes;
            return true;
        }

        void HeapCensus::noteForMap(uintptr_t regionStart, uintptr_t address, size_t bytes, bool used)
        {
            if (regionStart != mapRegion)
            {
                mapRegion = regionStart;
                pending = FreeSpan{};
            }
            if (used)
            {
                pending.usedBefore += bytes;
                ++pending.blocksBefore;
                if (bytes > pending.largestBefore.bytes)
                    pending.largestBefore = {address, bytes};
                return;
            }
            if (bytes < FREE_MAP_MIN_BYTES)
                return; // a small hole: counted with what is in use around it
            if (spans < FREE_MAP)
            {
                pending.address = address;
                pending.bytes = bytes;
                spanList[spans++] = pending;
            }
            pending = FreeSpan{};
        }

        void HeapCensus::writeFreeMap(ChunkText &out) const
        {
            line(out, "Free blocks of 1 KB or more, in address order (in use before each: bytes, blocks, largest):");
            for (size_t i = 0; i < spans; ++i)
            {
                const FreeSpan &s = spanList[i];
                const TaskStack *stack = stackAt(s.largestBefore.address);
                line(
                    out,
                    "  0x%08" PRIxPTR " %6u B  after %6u B in %3u blocks, largest %5u B%s%s%s",
                    s.address,
                    static_cast<unsigned>(s.bytes),
                    static_cast<unsigned>(s.usedBefore),
                    static_cast<unsigned>(s.blocksBefore),
                    static_cast<unsigned>(s.largestBefore.bytes),
                    stack != nullptr ? " (stack of " : "",
                    stack != nullptr ? stack->name : "",
                    stack != nullptr ? ")" : "");
            }
        }

        void HeapCensus::watch(TaskStack *tasks, size_t taskCount)
        {
            watched = tasks;
            watchedCount = tasks != nullptr ? taskCount : 0;
        }

        TaskStack *HeapCensus::stackAt(uintptr_t address) const
        {
            for (size_t i = 0; i < watchedCount; ++i)
                if (watched[i].base >= address && watched[i].base < address + BLOCK_HEADER_SLACK)
                    return &watched[i];
            return nullptr;
        }

        void HeapCensus::writeRegions(ChunkText &out) const
        {
            line(out, "Regions (start, size, free, largest free, blocks used/free):");
            for (size_t i = 0; i < regions; ++i)
            {
                const Region &r = regionList[i];
                line(
                    out,
                    "  0x%08" PRIxPTR " %7u B  free %6u  largest %6u  blocks %u/%u",
                    r.start,
                    static_cast<unsigned>(r.bytes),
                    static_cast<unsigned>(r.freeBytes),
                    static_cast<unsigned>(r.largestFree),
                    static_cast<unsigned>(r.usedBlocks),
                    static_cast<unsigned>(r.freeBlocks));
            }
        }

        void HeapCensus::writeBuckets(ChunkText &out) const
        {
            line(out, "Blocks by size (used count/bytes, free count/bytes):");
            for (size_t i = 0; i < BUCKETS; ++i)
                line(
                    out,
                    "  %-6s used %4u %7u B  free %4u %7u B",
                    BUCKET_NAMES[i],
                    static_cast<unsigned>(usedBuckets[i].count),
                    static_cast<unsigned>(usedBuckets[i].bytes),
                    static_cast<unsigned>(freeBuckets[i].count),
                    static_cast<unsigned>(freeBuckets[i].bytes));
        }

        void HeapCensus::writeTops(ChunkText &out) const
        {
            line(out, "Largest blocks in use:");
            for (size_t i = 0; i < tops; ++i)
            {
                const TaskStack *stack = stackAt(topBlocks[i].address);
                line(
                    out,
                    "  0x%08" PRIxPTR " %6u B%s%s",
                    topBlocks[i].address,
                    static_cast<unsigned>(topBlocks[i].bytes),
                    stack != nullptr ? "  stack of " : "",
                    stack != nullptr ? stack->name : "");
            }
        }

        void HeapCensus::writeTasks(ChunkText &out) const
        {
            line(out, "Task stacks (heap block, least left since start):");
            for (size_t i = 0; i < watchedCount; ++i)
            {
                const TaskStack &task = watched[i];
                char size[16] = "-";
                if (task.blockBytes > 0)
                    snprintf(size, sizeof(size), "%u", static_cast<unsigned>(task.blockBytes));
                line(out, "  %-16s %6s B  least left %6u B", task.name, size, static_cast<unsigned>(task.leastLeft));
            }
        }

        void HeapCensus::write(ChunkText &out) const
        {
            writeRegions(out);
            writeBuckets(out);
            writeTops(out);
            writeTasks(out);
            writeFreeMap(out);
        }
    } // namespace HeapSafe
} // namespace SQM
