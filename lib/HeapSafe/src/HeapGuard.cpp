#include "HeapGuard.h"

#include <cstring>
#include <initializer_list>

namespace SQM
{
    namespace HeapSafe
    {
        namespace
        {
            bool startsWith(const char *path, const char *prefix)
            {
                return std::strncmp(path, prefix, std::strlen(prefix)) == 0;
            }

            // `prefix` exactly, or followed by '/' or '?' (so "/setupx" isn't "/setup").
            bool under(const char *path, const char *prefix)
            {
                if (!startsWith(path, prefix))
                    return false;
                const char next = path[std::strlen(prefix)];
                return next == '\0' || next == '/' || next == '?';
            }
        } // namespace

        bool essential(const char *path, bool isPost)
        {
            if (path == nullptr)
                return false;
            for (const char *prefix : {"/api/v1", "/management", "/setup", "/api/update"})
                if (under(path, prefix))
                    return true;
            return isPost && startsWith(path, "/api/roof/");
        }

        bool admit(const char *path, bool isPost, uint32_t largestBlock, uint32_t freeHeap)
        {
            return essential(path, isPost) || (largestBlock >= MIN_BLOCK_BYTES && freeHeap >= MIN_FREE_BYTES);
        }

        bool frameFits(size_t length, uint32_t largestBlock)
        {
            return largestBlock >= FRAME_MARGIN_BYTES && largestBlock - FRAME_MARGIN_BYTES >= length;
        }
    } // namespace HeapSafe
} // namespace SQM
