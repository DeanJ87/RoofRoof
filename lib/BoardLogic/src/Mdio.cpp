#include "Mdio.h"

namespace SQM
{
    namespace Board
    {
        namespace Mdio
        {
            void readFrame(uint8_t phyAddress, uint8_t reg, bool (&bits)[DRIVEN_BITS])
            {
                size_t at = 0;
                for (size_t i = 0; i < PREAMBLE_BITS; ++i)
                    bits[at++] = true;
                // Start 01, read opcode 10.
                bits[at++] = false;
                bits[at++] = true;
                bits[at++] = true;
                bits[at++] = false;
                for (int bit = 4; bit >= 0; --bit)
                    bits[at++] = ((phyAddress >> bit) & 1u) != 0;
                for (int bit = 4; bit >= 0; --bit)
                    bits[at++] = ((reg >> bit) & 1u) != 0;
            }

            bool decodeRead(const bool (&reply)[REPLY_BITS], uint16_t &value)
            {
                if (reply[0])
                    return false;
                value = 0;
                for (size_t i = 1; i < REPLY_BITS; ++i)
                    value = static_cast<uint16_t>((value << 1) | (reply[i] ? 1u : 0u));
                return true;
            }

            Phy identify(uint16_t id1, uint16_t id2)
            {
                if ((id1 == 0xFFFF && id2 == 0xFFFF) || (id1 == 0 && id2 == 0))
                    return Phy::None;
                if (id1 == 0x0007 && (id2 & 0xFFF0) == 0xC0F0)
                    return Phy::Lan87xx;
                return Phy::Other;
            }

            bool clockSeen(const bool *samples, size_t count)
            {
                bool high = false;
                bool low = false;
                for (size_t i = 0; i < count && samples != nullptr; ++i)
                    (samples[i] ? high : low) = true;
                return high && low;
            }
        } // namespace Mdio
    } // namespace Board
} // namespace SQM
