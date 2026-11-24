#include "SwitchWatch.h"

namespace SQM
{
    namespace Safety
    {
        bool watchReadings(SwitchWatch &watch, uint8_t fitted, SwitchReading switch1, SwitchReading switch2)
        {
            const SwitchReading readings[] = {switch1, switch2};
            bool changed = false;
            for (uint8_t i = 0; i < fitted && i < 2; ++i)
            {
                if (readings[i] == SwitchReading::Open && watch.quiet[i] != 0)
                {
                    watch.quiet[i] = 0;
                    changed = true;
                }
            }
            return changed;
        }

        bool countOpening(SwitchWatch &watch, uint8_t fitted)
        {
            bool changed = false;
            for (uint8_t i = 0; i < fitted && i < 2; ++i)
            {
                if (watch.quiet[i] < UINT8_MAX)
                {
                    ++watch.quiet[i];
                    changed = true;
                }
            }
            return changed;
        }

        uint8_t stuckSwitch(const SwitchWatch &watch, uint8_t fitted, bool switchesOnly)
        {
            if (!switchesOnly)
                return 0;
            for (uint8_t i = 0; i < fitted && i < 2; ++i)
                if (watch.quiet[i] >= STUCK_OPENINGS)
                    return static_cast<uint8_t>(i + 1);
            return 0;
        }
    } // namespace Safety
} // namespace SQM
