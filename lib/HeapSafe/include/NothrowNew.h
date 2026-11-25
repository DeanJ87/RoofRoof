#pragma once

// Spec 037: this library replaces the nothrow forms of operator new and
// new[] (lib/HeapSafe/src/NothrowNew.cpp) so they return nullptr when memory runs out
// instead of aborting in a build without exceptions. Nothing to call: include
// this where the replacement must be linked in, the way the web server's
// accept path and the native test do.

namespace SQM
{
    namespace HeapSafe
    {
        // Referenced from the firmware so the linker always keeps the replacement.
        extern const bool nothrowNewReplaced;
    } // namespace HeapSafe
} // namespace SQM
