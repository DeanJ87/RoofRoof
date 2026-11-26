#pragma once

// Spec 043: the roof's position for the telescope as an ASCOM Alpaca Switch
// (ISwitchV3, device 0): read-only boolean switches a N.I.N.A. sequence can
// gate its slews on ("Mount may move": the roof at an end). The Alpaca side
// knows nothing about the roof: the backend fills the readings (lib/RoofIoLogic
// MountMove), so the demo and the simulator compile the router without it.

#include <string>

namespace SQM
{
    namespace Alpaca
    {
        // One read-only switch: its fixed name and description, and its value now.
        struct SwitchReading
        {
            std::string name;
            std::string description;
            bool value = false;
        };
    } // namespace Alpaca
} // namespace SQM
