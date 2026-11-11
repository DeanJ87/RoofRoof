#include "Config.h"
#include "ConfigInternal.h"

// The defaults. JSON is in ConfigJsonOut.cpp and ConfigJsonIn.cpp, validation in
// ConfigValidate.cpp.

namespace SQM
{
    namespace
    {
        void defaultNetwork(Config &cfg)
        {
            cfg.wifi.ssid = "";
            cfg.wifi.password = "";
            cfg.wifi.hostname = "roofroof";
            cfg.wifi.mdns = true;
            cfg.wifi.ipv6 = true;
            cfg.wifi.autoReconnect = true;
            cfg.wifi.reconnectDelayMs = 1000;
            cfg.wifi.maxReconnectDelayMs = 300000; // 5 minutes

            cfg.mqtt.enabled = false;
            cfg.mqtt.broker = "";
            cfg.mqtt.port = 1883;
            cfg.mqtt.username = "";
            cfg.mqtt.password = "";
            cfg.mqtt.topic = "roofroof";
            cfg.mqtt.homeAssistant = false;
            cfg.mqtt.discoveryPrefix = "homeassistant";
            cfg.mqtt.roofCommands = false;
            cfg.mqtt.publishIntervalMs = 60000; // 1 minute

            cfg.ota.enabled = false;
            cfg.ota.password = "";

            cfg.auth.enabled = false;
            cfg.auth.username = "admin";
            cfg.auth.password = "";
        }

        void defaultTime(Config &cfg)
        {
            cfg.ntp.enabled = true;
            cfg.ntp.server1 = "pool.ntp.org";
            cfg.ntp.server2 = "time.nist.gov";
            cfg.ntp.timezone = "UTC0";       // POSIX format
            cfg.ntp.syncIntervalMs = 600000; // 10 minutes
        }

        void defaultAlerts(AlertsConfig &a)
        {
            a.enabled = false;
            a.mountNotParked = {4, "", "", ""};
            a.roofNotClosed = {4, "", "", ""};
            a.moveFault = {3, "", "", ""};
            a.supplyLow = {2, "", "", ""};
            a.unsafeClosing = {2, "", "", ""};
            a.roofClosed = {2, "", "", ""};
            a.safetyLost = {2, "", "", ""};
            a.clientLost = {3, "", "", ""};
            a.clientBack = {1, "", "", ""};
            a.clientDisconnected = {0, "", "", ""};
            a.daylightOpen = {3, "", "", ""};
            a.daylightClosed = {2, "", "", ""};
            a.daylightUnknown = {2, "", "", ""};
            a.sendMode = AlertsConfig::SendMode::Any;
            a.clientSilentSeconds = 120;
            a.cooldownSeconds = 300;
            a.pushoverEnabled = false;
            a.ntfyEnabled = false;
            a.ntfyServer = "https://ntfy.sh";
            a.webhookEnabled = false;
            a.webhookInsecureTls = false;
            a.mqttEnabled = false;
        }
    } // namespace

    void AlertsConfig::raiseCriticalToMinimum()
    {
        for (EventSetting *event : {&mountNotParked, &roofNotClosed})
        {
            if (event->level > 4)
                event->level = 4;
            else if (event->level < MIN_CRITICAL_LEVEL)
                event->level = MIN_CRITICAL_LEVEL;
            if (!audibleSound(event->sound))
                event->sound.clear();
        }
    }

    Config Config::createDefault()
    {
        Config cfg;
        cfg.deviceName = "RoofRoof";
        defaultNetwork(cfg);
        defaultTime(cfg);
        cfg.alpaca.enabled = false;
        defaultAlerts(cfg.alerts);
        // Every safety source off and N.I.N.A. not configured: the verdict is
        // unsafe and the mount not parked until they are set up (spec 004).
        cfg.safety = SafetyConfig{};
        cfg.park = ParkConfig{};
        cfg.roof = RoofConfig();
        cfg.daylight = DaylightConfig{};
        cfg.location = LocationConfig{};
        cfg.setup = SetupConfig{}; // new: the wizard opens after the first network join (spec 015)
        return cfg;
    }

    bool Config::storedBeforeSetup(const std::string &storedJson)
    {
        StaticJsonDocument<64> filter;
        filter["setup"] = true;
        DynamicJsonDocument doc(256);
        if (deserializeJson(doc, storedJson, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
            return false; // unreadable: load() reports it; nothing to mark
        return !doc["setup"].is<JsonObjectConst>();
    }

    void Config::markStoredBeforeSetup()
    {
        setup.state = SetupConfig::State::Offered;
        setup.step = 0;
    }

    std::string Config::spliceStored(const std::string &main, const std::string &alerts, const std::string &client)
    {
        std::string spliced = main;
        const std::pair<const char *, const std::string *> parts[] = {
            {"alerts", &alerts},
            {"alertsClient", &client},
        };
        for (const auto &part : parts)
        {
            const size_t close = spliced.rfind('}');
            if (part.second->empty() || close == std::string::npos)
                continue;
            spliced.insert(close, std::string(",\"") + part.first + "\":" + *part.second);
        }
        return spliced;
    }

    std::optional<Config> Config::fromJson(const std::string &json, const Config *baseConfig, std::string *error)
    {
        std::optional<Config> cfg(baseConfig != nullptr ? *baseConfig : createDefault());
        if (!applyJson(json, *cfg, baseConfig != nullptr, error))
            return std::nullopt;
        return cfg;
    }
} // namespace SQM
