#pragma once

// The roof as an ASCOM Alpaca Dome with a shutter only (spec 006,
// contracts/alpaca-dome.md). The Alpaca side knows nothing about the roof:
// the backend maps these to the roof (lib/RoofIoLogic DomeBridge), so the
// demo can compile the router without the roof.

#include <cstdint>
#include <string>

namespace SQM
{
    namespace Alpaca
    {
        // ASCOM ShutterState, with its numbers.
        enum class ShutterState : uint8_t
        {
            Open = 0,
            Closed = 1,
            Opening = 2,
            Closing = 3,
            Error = 4,
        };

        struct DomeStatus
        {
            ShutterState shutter = ShutterState::Error;
            bool slewing = false;
        };

        // The only Dome members that act.
        enum class DomeAction : uint8_t
        {
            OpenShutter,
            CloseShutter,
            AbortSlew,
        };

        // What the roof said to a Dome action: refused ones carry the reason
        // (constitution III: the value and the limit, or the input that failed).
        struct DomeAnswer
        {
            bool accepted = true;
            std::string message;
        };

    } // namespace Alpaca
} // namespace SQM
