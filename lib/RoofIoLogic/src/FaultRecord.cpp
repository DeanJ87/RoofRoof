#include "FaultRecord.h"

#include <cstring>

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            constexpr uint8_t MAGIC_0 = 'R';
            constexpr uint8_t MAGIC_1 = 'F';
            constexpr size_t CHECKED_BYTES = 14; // the checksum covers bytes 0-13
            constexpr uint8_t LAST_KIND = static_cast<uint8_t>(Roof::FaultKind::External);
            constexpr uint8_t LAST_CAUSE = static_cast<uint8_t>(FaultCause::BenchSupply);

            uint16_t checksum(const uint8_t *data)
            {
                uint16_t sum = 0;
                for (size_t i = 0; i < CHECKED_BYTES; ++i)
                    sum = static_cast<uint16_t>(sum + data[i]);
                return static_cast<uint16_t>(~sum);
            }

            void putFloat(uint8_t *at, float value)
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                for (int i = 0; i < 4; ++i)
                    at[i] = static_cast<uint8_t>(bits >> (8 * i));
            }

            float getFloat(const uint8_t *at)
            {
                uint32_t bits = 0;
                for (int i = 0; i < 4; ++i)
                    bits |= static_cast<uint32_t>(at[i]) << (8 * i);
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }
        } // namespace

        void encodeFault(const FaultRecord &record, uint8_t (&out)[FAULT_RECORD_BYTES])
        {
            std::memset(out, 0, sizeof(out));
            out[0] = MAGIC_0;
            out[1] = MAGIC_1;
            out[2] = FAULT_RECORD_VERSION;
            out[3] = static_cast<uint8_t>(record.fault.kind);
            out[4] = static_cast<uint8_t>(record.cause);
            putFloat(out + 6, record.fault.measured);
            putFloat(out + 10, record.fault.limit);
            const uint16_t sum = checksum(out);
            out[14] = static_cast<uint8_t>(sum & 0xFF);
            out[15] = static_cast<uint8_t>(sum >> 8);
        }

        Decoded decodeFault(const uint8_t *data, size_t length, FaultRecord &out)
        {
            if (length == 0)
                return Decoded::None;
            out = storedUnreadableRecord();
            if (data == nullptr || length != FAULT_RECORD_BYTES)
                return Decoded::Unreadable;
            const uint16_t stored = static_cast<uint16_t>(data[14] | (data[15] << 8));
            const bool header = data[0] == MAGIC_0 && data[1] == MAGIC_1 && data[2] == FAULT_RECORD_VERSION && data[5] == 0;
            const bool kindOk = data[3] != 0 && data[3] <= LAST_KIND;
            const bool causeOk = data[4] <= LAST_CAUSE;
            if (!header || !kindOk || !causeOk || stored != checksum(data))
                return Decoded::Unreadable;
            out.fault.kind = static_cast<Roof::FaultKind>(data[3]);
            out.cause = static_cast<FaultCause>(data[4]);
            out.fault.measured = getFloat(data + 6);
            out.fault.limit = getFloat(data + 10);
            return Decoded::Fault;
        }

        FaultRecord storedUnreadableRecord()
        {
            return externalRecord(FaultCause::StoredUnreadable, 0.0f, 0.0f);
        }

        FaultRecord externalRecord(FaultCause cause, float measured, float limit)
        {
            FaultRecord record;
            record.fault.kind = Roof::FaultKind::External;
            record.fault.measured = measured;
            record.fault.limit = limit;
            record.cause = cause;
            return record;
        }

        const char *causeName(FaultCause cause)
        {
            switch (cause)
            {
            case FaultCause::Logic:
                return "logic";
            case FaultCause::Watchdog:
                return "watchdog";
            case FaultCause::ParkInterlock:
                return "parkInterlock";
            case FaultCause::StoredUnreadable:
                return "storedUnreadable";
            case FaultCause::WatchdogStopped:
                return "watchdogStopped";
            case FaultCause::BenchSupply:
                return "benchSupply";
            }
            return "unknown";
        }
    } // namespace RoofIo
} // namespace SQM
