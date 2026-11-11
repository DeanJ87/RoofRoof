#include "DeviceCore.h"

#include <ctime>

// Formatting shared by the device's documents and alerts. The alert side is in
// DeviceAlerts.cpp.

namespace SQM
{
    namespace Core
    {
        std::string isoUtc(int64_t epoch)
        {
            if (epoch < CLOCK_VALID_EPOCH)
                return "";
            const time_t t = static_cast<time_t>(epoch);
            struct tm utc;
            gmtime_r(&t, &utc);
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
            return buffer;
        }

        std::string formatDuration(uint32_t seconds)
        {
            if (seconds < 60)
                return std::to_string(seconds) + " s";
            const uint32_t minutes = (seconds + 30) / 60;
            if (minutes < 60)
                return std::to_string(minutes) + " min";
            return std::to_string(minutes / 60) + " h" + (minutes % 60 != 0 ? " " + std::to_string(minutes % 60) + " min" : "");
        }
    } // namespace Core
} // namespace SQM
