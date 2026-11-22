#include "ParkRule.h"

#include "SafetyInput.h"

// Spec 021: a close rides through a missed N.I.N.A. reply on the last good
// reading while it is fresh; any evidence of "not parked" still stops it.

namespace SQM
{
    namespace Safety
    {
        namespace
        {
            using Nina::ReadingStatus;

            // A poll that got no usable answer: absence of evidence, not a report.
            bool readFailed(ReadingStatus status)
            {
                return status == ReadingStatus::Unreachable || status == ReadingStatus::Unreadable || status == ReadingStatus::ApiError;
            }

            constexpr uint32_t READ_FAILURES = ParkNinaUnreachable | ParkNinaUnreadable | ParkNinaApiError;
        } // namespace

        void rememberNina(NinaMemory &memory, const Nina::MountReading &reading, uint32_t generation)
        {
            if (memory.known && memory.generation != generation)
                memory = NinaMemory();
            if (reading.status == ReadingStatus::Good)
            {
                memory.lastGood = reading;
                memory.generation = generation;
                memory.known = true;
            }
            else if (!readFailed(reading.status))
                memory = NinaMemory(); // not configured, or not read yet (a new address)
        }

        CloseHold closeHold(const ParkInputs &inputs, const ParkResult &result, const NinaMemory &memory)
        {
            CloseHold hold;
            if (result.parked)
            {
                hold.parked = true;
                hold.freshUntilMs = inputs.nina.takenAtMs + inputs.ninaMaxAgeMs;
                hold.ageMs = result.ninaAgeMs;
                return hold;
            }
            // Only a failed read, and nothing else, keeps the park from being confirmed.
            if (!memory.known || !readFailed(inputs.nina.status) || (result.reasons & ~READ_FAILURES) != 0)
                return hold;
            ParkInputs remembered = inputs;
            remembered.nina = memory.lastGood;
            const ParkResult before = evaluatePark(remembered);
            if (!before.parked)
                return hold;
            hold.riding = true;
            hold.freshUntilMs = memory.lastGood.takenAtMs + inputs.ninaMaxAgeMs;
            hold.ageMs = before.ninaAgeMs;
            return hold;
        }

        std::string describe(const CloseHold &hold, uint32_t limitMs)
        {
            if (!hold.riding)
                return "";
            return "riding on a parked reading " + formatSeconds(hold.ageMs) + " old (limit " + formatSeconds(limitMs) + ")";
        }
    } // namespace Safety
} // namespace SQM
