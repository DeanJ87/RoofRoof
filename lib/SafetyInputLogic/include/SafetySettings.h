#pragma once

// Which safety records a settings save restarts (spec 004 T029). A save that
// doesn't touch a source keeps its readings; one that does starts that source
// from nothing, so a reply to the old URL or topic never counts for the new one.

#include <cstdint>
#include <string>

namespace SQM
{
    namespace Safety
    {
        struct SettingsChange
        {
            bool sqmeterHttp = true; // URL or on/off: the HTTP record restarts
            bool sqmeterMqtt = true; // topic or on/off: the MQTT record restarts, topics re-watched
            bool nina = true;        // URL or API: the mount reading and park reply restart
            bool window = true;      // any weather-source setting: the start-up window restarts
        };

        // The settings that feed the sources (Config's safety and park sections).
        struct SourceSettings
        {
            bool httpEnabled = false;
            std::string httpUrl;
            bool mqttEnabled = false;
            std::string mqttTopic;
            uint32_t staleSeconds = 0;
            bool rainContact = false;
            std::string ninaUrl;
            uint8_t ninaApi = 0;
        };

        // Everything restarts on the first call (`before` == nullptr).
        SettingsChange settingsChange(const SourceSettings *before, const SourceSettings &after);
    } // namespace Safety
} // namespace SQM
