#pragma once

#include <cstdint>
#include <string>

namespace SQM
{
    // Spec 024: SQMeter's secrets the device read during a "copy from
    // SQMeter", held in memory for a while, with the servers they belong to. A
    // secret field saved as Config::COPIED_SECRET takes the held value, only
    // for that server (Config::applyJson).
    struct CopiedSecrets
    {
        std::string mqttBroker;
        uint16_t mqttPort = 0;
        std::string mqttUsername;
        std::string mqttPassword;
        std::string ntfyServer;
        std::string ntfyToken;
        std::string pushoverUserKey;
        std::string pushoverAppToken;
    };
} // namespace SQM
