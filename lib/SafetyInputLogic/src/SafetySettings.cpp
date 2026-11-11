#include "SafetySettings.h"

namespace SQM
{
    namespace Safety
    {
        SettingsChange settingsChange(const SourceSettings *before, const SourceSettings &after)
        {
            SettingsChange change;
            if (before == nullptr)
                return change;
            const SourceSettings &b = *before;
            change.sqmeterHttp = b.httpEnabled != after.httpEnabled || b.httpUrl != after.httpUrl;
            change.sqmeterMqtt = b.mqttEnabled != after.mqttEnabled || b.mqttTopic != after.mqttTopic;
            change.nina = b.ninaUrl != after.ninaUrl || b.ninaApi != after.ninaApi;
            change.window =
                change.sqmeterHttp || change.sqmeterMqtt || b.staleSeconds != after.staleSeconds || b.rainContact != after.rainContact;
            return change;
        }
    } // namespace Safety
} // namespace SQM
