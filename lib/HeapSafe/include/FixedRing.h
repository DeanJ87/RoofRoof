#pragma once

// Spec 045: records kept for a while (the recent alerts) in room taken once.
//
// The recent alerts were a vector of records holding std::strings and a list
// of template values: each alert allocated a dozen blocks that lived until
// twenty more alerts had come, and each was taken amid the alert's own
// short-lived ones (its MQTT text, the queue's copy), so every alert left
// small blocks scattered across the heap's largest free block. Here the
// records are fixed-size and their room is static data: adding one copies it
// in, the oldest is overwritten when full, and nothing is allocated or freed.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SQM
{
    namespace HeapSafe
    {
        // Copies `text` into `out` (`size` bytes with its terminator), cut at
        // a UTF-8 character boundary when it doesn't fit, the cut marked with
        // "..." when there is room for it. Null `text` is empty.
        void copyCut(char *out, size_t size, const char *text);
        void copyCut(char *out, size_t size, const char *text, size_t length);

        template <typename T, size_t N> class FixedRing
        {
        public:
            static_assert(N > 0, "a ring holds at least one record");
            static constexpr size_t CAPACITY = N;

            // A copy of `record` as the newest; the oldest is overwritten when
            // full. Returns the stored copy.
            T &push(const T &record)
            {
                const size_t slot = (first + count) % N;
                if (count == N)
                    first = (first + 1) % N;
                else
                    ++count;
                items[slot] = record;
                return items[slot];
            }

            size_t size() const { return count; }
            bool empty() const { return count == 0; }
            void clear()
            {
                first = 0;
                count = 0;
            }

            // Oldest first: at(0) is the oldest, at(size() - 1) the newest.
            T &at(size_t i) { return items[(first + i) % N]; }
            const T &at(size_t i) const { return items[(first + i) % N]; }

            // The first record `match` accepts, newest first; nullptr if none.
            template <typename Match> T *findNewest(Match match)
            {
                for (size_t i = count; i > 0; --i)
                    if (match(at(i - 1)))
                        return &at(i - 1);
                return nullptr;
            }

        private:
            T items[N] = {};
            size_t first = 0;
            size_t count = 0;
        };
    } // namespace HeapSafe
} // namespace SQM
