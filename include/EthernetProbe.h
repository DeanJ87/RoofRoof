#pragma once

// The WT32-ETH01 probe (spec 023 FR-018, FR-034, research R7): the board's
// 50 MHz oscillator on for about 15 ms, the clock on GPIO0, the LAN8720's ID
// registers at address 1 over MDC 23 / MDIO 18, then all of them released.
// Those pins aren't used by the DevKit's wiring, so probing a DevKit is harmless.

#include "WiringCheck.h"

namespace SQM
{
    namespace EthernetProbe
    {
        // Fills the probe's fields (mode Probed). The caller makes sure none
        // of GPIO 0, 16, 18, 23 is in use and the chip has no PSRAM.
        void run(Board::EthernetFacts &facts);
        // A LAN8720 answered.
        bool foundWt32(const Board::EthernetFacts &facts);
    } // namespace EthernetProbe
} // namespace SQM
