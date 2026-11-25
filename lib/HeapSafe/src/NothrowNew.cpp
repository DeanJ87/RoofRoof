// Spec 037: new (std::nothrow) returns nullptr when memory runs out.
//
// The toolchain's C++ library implements the nothrow forms by calling the
// throwing operator new and catching std::bad_alloc. This firmware is built
// without exceptions, so that throw aborts instead: a library that checks for
// nullptr (the web server creating a request for a new connection, spec 031's
// replies) restarted the device rather than refusing one request. These
// replacements allocate with malloc, like the throwing forms do, so the
// default operator delete still frees them.

#include <cstdlib>
#include <new>

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return std::malloc(size == 0 ? 1 : size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &) noexcept
{
    std::free(ptr);
}

#include "NothrowNew.h"

namespace SQM
{
    namespace HeapSafe
    {
        const bool nothrowNewReplaced = true;
    } // namespace HeapSafe
} // namespace SQM
