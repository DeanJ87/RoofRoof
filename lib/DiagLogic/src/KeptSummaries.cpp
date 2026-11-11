#include "KeptSummaries.h"

#include <cstring>

namespace SQM
{
    namespace Diag
    {
        namespace
        {
            constexpr uint8_t MAGIC[4] = {'R', 'R', 'D', 'G'};

            void put32(uint8_t *at, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                    at[i] = static_cast<uint8_t>(value >> (8 * i));
            }

            uint32_t get32(const uint8_t *at)
            {
                uint32_t value = 0;
                for (int i = 3; i >= 0; --i)
                    value = (value << 8) | at[i];
                return value;
            }

            void putFloat(uint8_t *at, float value)
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                put32(at, bits);
            }

            float getFloat(const uint8_t *at)
            {
                const uint32_t bits = get32(at);
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0xFFFFFFFFu)
            {
                for (size_t i = 0; i < size; ++i)
                {
                    crc ^= data[i];
                    for (int bit = 0; bit < 8; ++bit)
                        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
                }
                return crc;
            }

            // The version, count and next id (header bytes 4-11) and the records.
            uint32_t fileCrc(const uint8_t *data, size_t size)
            {
                const uint32_t head = crc32(data + 4, 8);
                return ~crc32(data + KEPT_HEADER_BYTES, size - KEPT_HEADER_BYTES, head);
            }

            void encodeRecord(const Summary &s, const Clock &clock, uint8_t *at)
            {
                put32(at, s.id);
                put32(at + 4, s.kept ? s.startEpoch : epochOf(clock, s.startMs));
                put32(at + 8, s.kept ? s.endEpoch : epochOf(clock, s.endMs));
                put32(at + 12, s.durationMs());
                put32(at + 16, s.drivenMs);
                const float floats[] = {s.faultMeasured, s.faultLimit, s.peakAmps, s.averageAmps, s.stallAmps, s.restVolts, s.minVolts};
                for (size_t i = 0; i < 7; ++i)
                    putFloat(at + 20 + 4 * i, floats[i]);
                at[48] = static_cast<uint8_t>(s.hallStart);
                at[49] = static_cast<uint8_t>(s.hallStart >> 8);
                at[50] = static_cast<uint8_t>(s.hallEnd);
                at[51] = static_cast<uint8_t>(s.hallEnd >> 8);
                at[52] = static_cast<uint8_t>(s.origin);
                at[53] = static_cast<uint8_t>(s.target);
                at[54] = static_cast<uint8_t>(s.endState);
                at[55] = static_cast<uint8_t>(static_cast<uint8_t>(s.result) | (s.fromUnknown ? 4 : 0) | (static_cast<uint8_t>(s.fault) << 3));
            }

            bool decodeRecord(const uint8_t *at, Summary &s)
            {
                const uint8_t result = at[55] & 3;
                const uint8_t fault = static_cast<uint8_t>(at[55] >> 3);
                if (at[52] > static_cast<uint8_t>(Roof::Origin::Mqtt) || at[53] > 1 || at[54] > static_cast<uint8_t>(Roof::State::Error) ||
                    result > static_cast<uint8_t>(Result::Fault) || fault > static_cast<uint8_t>(Roof::FaultKind::External))
                    return false;
                s = Summary();
                s.kept = true;
                s.id = get32(at);
                s.startEpoch = get32(at + 4);
                s.endEpoch = get32(at + 8);
                s.endMs = get32(at + 12); // no uptime: the duration from 0
                s.drivenMs = get32(at + 16);
                float *floats[] = {&s.faultMeasured, &s.faultLimit, &s.peakAmps, &s.averageAmps, &s.stallAmps, &s.restVolts, &s.minVolts};
                for (size_t i = 0; i < 7; ++i)
                    *floats[i] = getFloat(at + 20 + 4 * i);
                s.hallStart = static_cast<uint16_t>(at[48] | (at[49] << 8));
                s.hallEnd = static_cast<uint16_t>(at[50] | (at[51] << 8));
                s.origin = static_cast<Roof::Origin>(at[52]);
                s.target = static_cast<Roof::End>(at[53]);
                s.endState = static_cast<Roof::State>(at[54]);
                s.result = static_cast<Result>(result);
                s.fromUnknown = (at[55] & 4) != 0;
                s.fault = static_cast<Roof::FaultKind>(fault);
                return true;
            }
        } // namespace

        size_t encodeKept(const SummaryList &list, uint32_t nextId, const Clock &clock, uint8_t *out, size_t capacity)
        {
            const size_t bytes = KEPT_HEADER_BYTES + list.size() * KEPT_RECORD_BYTES;
            if (capacity < bytes)
                return 0;
            std::memset(out, 0, bytes);
            std::memcpy(out, MAGIC, sizeof(MAGIC));
            out[4] = KEPT_VERSION;
            out[5] = list.size();
            put32(out + 8, nextId);
            for (uint8_t i = 0; i < list.size(); ++i)
                encodeRecord(list.at(i), clock, out + KEPT_HEADER_BYTES + i * KEPT_RECORD_BYTES);
            put32(out + 12, fileCrc(out, bytes));
            return bytes;
        }

        bool decodeKept(const uint8_t *data, size_t size, SummaryList &out, uint32_t &nextId)
        {
            if (data == nullptr || size < KEPT_HEADER_BYTES || std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0 || data[4] != KEPT_VERSION)
                return false;
            const uint8_t count = data[5];
            if (count > MAX_SUMMARIES || size != KEPT_HEADER_BYTES + count * KEPT_RECORD_BYTES)
                return false;
            if (fileCrc(data, size) != get32(data + 12))
                return false;
            // Every record checked first, so a bad one leaves `out` unchanged;
            // no list of 32 summaries on the caller's stack.
            Summary record;
            uint32_t highest = 0;
            for (uint8_t i = 0; i < count; ++i)
            {
                if (!decodeRecord(data + KEPT_HEADER_BYTES + i * KEPT_RECORD_BYTES, record))
                    return false;
                if (record.id > highest)
                    highest = record.id;
            }
            out.clear();
            for (int i = count - 1; i >= 0; --i) // oldest first: add() puts each at the front
            {
                decodeRecord(data + KEPT_HEADER_BYTES + i * KEPT_RECORD_BYTES, record);
                out.add(record);
            }
            const uint32_t stored = get32(data + 8);
            nextId = stored > highest ? stored : highest + 1;
            return true;
        }

        bool KeptWrite::due(uint32_t nowMs, uint32_t revision, bool settingOn) const
        {
            if (!settingOn || revision == writtenRevision)
                return false;
            return urgent || !any || nowMs - lastMs >= KEPT_WRITE_MIN_MS;
        }

        void KeptWrite::written(uint32_t nowMs, uint32_t revision)
        {
            any = true;
            urgent = false;
            lastMs = nowMs;
            writtenRevision = revision;
        }
    } // namespace Diag
} // namespace SQM
