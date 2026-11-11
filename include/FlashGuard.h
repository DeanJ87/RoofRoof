#pragma once

// Flash writes in progress (web uploads, command-line OTA), for the roof: open
// waits for them, and a close stops them (spec 003 FR-029; the decisions are
// RoofIo::FlashActivity's). Any task.

#include <cstdint>

namespace SQM
{
    namespace FlashGuard
    {
        uint32_t begin();
        void progress(uint32_t token);
        void end(uint32_t token);
        // Writing, and made progress within RoofIo::FLASH_STALE_MS.
        bool busy();
        // A close wants the flash: the writer stops at its next chunk.
        void requestStop();
        bool shouldStop(uint32_t token);
    } // namespace FlashGuard
} // namespace SQM
