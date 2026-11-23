#pragma once

// IEEE 802.3 clause 22 management reads, bit by bit, for the wiring check's
// Ethernet probe (spec 023 FR-018, research R7). The firmware clocks the bits
// out on MDC and samples MDIO; the frames and the decoding are here.

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace Board
    {
        namespace Mdio
        {
            // A read frame: what the station drives (preamble, start, read
            // opcode, PHY address, register), then the PHY's turnaround and data.
            constexpr size_t PREAMBLE_BITS = 32;
            constexpr size_t DRIVEN_BITS = PREAMBLE_BITS + 2 + 2 + 5 + 5; // 46
            // Read as Linux's mdio-bitbang does: each bit after the falling MDC
            // edge that follows the PHY launching it, so the first sample is the
            // turnaround's second bit (the PHY's 0), then 16 data bits.
            constexpr size_t REPLY_BITS = 1 + 16;

            // The bits the station drives, first to last (true = released/high).
            void readFrame(uint8_t phyAddress, uint8_t reg, bool (&bits)[DRIVEN_BITS]);

            // The PHY's reply as sampled: the turnaround's bit must be driven
            // low by the PHY (a missing PHY leaves the line pulled high), then
            // 16 data bits, most significant first.
            bool decodeRead(const bool (&reply)[REPLY_BITS], uint16_t &value);

            // The ID registers (2 and 3).
            constexpr uint8_t REG_ID1 = 2;
            constexpr uint8_t REG_ID2 = 3;

            enum class Phy : uint8_t
            {
                None,    // all ones or all zeros: nothing answered
                Lan87xx, // Microchip/SMSC LAN8720A (and LAN8710A): 0x0007 / 0xC0Fx
                Other,   // some PHY answered
            };
            Phy identify(uint16_t id1, uint16_t id2);

            // A running 50 MHz clock sampled far slower than it runs shows both levels.
            bool clockSeen(const bool *samples, size_t count);
        } // namespace Mdio
    } // namespace Board
} // namespace SQM
