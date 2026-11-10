#pragma once

#include <cstdint>

#include "AlpacaRouter.h"

namespace SQM
{
    namespace Alpaca
    {
        // Why the device last started, as far as the imaging app is concerned.
        enum class ResetKind : uint8_t
        {
            PowerOn,  // power applied (or a brownout): a fresh start
            External, // the reset button or the USB serial adapter
            Software, // the device restarted itself (settings, update)
            Panic,    // a crash
            Watchdog, // a task stopped responding
            Brownout,
            DeepSleep,
            Unknown,
        };

        // The device restarted on its own: an imaging app that had a device
        // connected still wants it, so the connection is restored after the
        // restart (SQMeter spec 011 FR-008). A power cut or the reset button starts
        // fresh.
        bool keepsConnections(ResetKind kind);

        // Which devices an imaging app has connected, kept in memory that
        // survives a restart without power loss. `check` guards against
        // leftovers from a different firmware or random power-on contents.
        struct ConnectionMemory
        {
            uint32_t magic = 0;
            uint8_t connected = 0; // bit per device index
            uint8_t check = 0;
        };

        ConnectionMemory rememberConnections(const bool (&connected)[MAX_DEVICES]);

        // False when the memory doesn't hold a valid record.
        bool recallConnections(const ConnectionMemory &memory, bool (&connected)[MAX_DEVICES]);

    } // namespace Alpaca
} // namespace SQM
