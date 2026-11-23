#include "ConfigSave.h"

#include <ArduinoJson.h>

// Spec 022: the test box's deadline, stamped by the settings save
// (data-model.md, "The save stamp").

namespace SQM
{
    namespace ConfigSave
    {
        bool stampTestBox(const TestBoxConfig &live, TestBoxConfig &next, bool hoursSent, const Clock &clock, std::string *error)
        {
            if (!next.any())
            {
                next.until = 0;
                return true;
            }
            const bool turnedOn = (next.park && !live.park) || (next.weather && !live.weather) || (next.daylight && !live.daylight);
            if (!turnedOn && !hoursSent)
            {
                next.until = live.until;
                return true;
            }
            if (!clock.set)
            {
                if (error != nullptr)
                    *error = "Test box: the clock isn't set yet (Settings → Time); a bypass needs a deadline";
                return false;
            }
            const uint64_t until = static_cast<uint64_t>(clock.epoch) + static_cast<uint64_t>(next.hours) * 3600u;
            next.until = until > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(until);
            return true;
        }

        bool sendsTestBoxHours(const std::string &body)
        {
            StaticJsonDocument<64> filter;
            filter["testBox"]["hours"] = true;
            DynamicJsonDocument doc(256);
            if (deserializeJson(doc, body, DeserializationOption::Filter(filter)))
                return false;
            return doc["testBox"].is<JsonObject>() && doc["testBox"].containsKey("hours");
        }
    } // namespace ConfigSave
} // namespace SQM
