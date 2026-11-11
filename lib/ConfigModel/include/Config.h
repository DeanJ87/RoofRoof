#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace SQM
{

    struct WiFiConfig
    {
        std::string ssid;
        std::string password;
        std::string hostname;
        bool mdns = true; // advertise <hostname>.local and the HTTP service
        bool ipv6 = true; // dual stack on the joined network (SQMeter spec 015); applies at restart
        bool autoReconnect;
        uint32_t reconnectDelayMs;
        uint32_t maxReconnectDelayMs;
    };

    struct MQTTConfig
    {
        bool enabled;
        std::string broker;
        uint16_t port;
        std::string username;
        std::string password;
        std::string topic; // base topic: <topic>/availability, /alerts, ...
        // How often periodic topics are republished. Spec 008 adds the roof's
        // topics and the groups that choose them.
        uint32_t publishIntervalMs;
        bool homeAssistant = false; // MQTT discovery
        std::string discoveryPrefix = "homeassistant";
    };

    struct OTAConfig
    {
        bool enabled;
        std::string password;
    };

    struct AuthConfig
    {
        bool enabled;
        std::string username;
        std::string password;
    };

    struct NTPConfig
    {
        bool enabled;
        std::string server1;     // Primary NTP server (e.g., "pool.ntp.org")
        std::string server2;     // Secondary NTP server (optional fallback)
        std::string timezone;    // POSIX timezone string (e.g., "PST8PDT,M3.2.0,M11.1.0")
        uint32_t syncIntervalMs; // How often to sync with NTP
    };

    // ASCOM Alpaca: discovery, the management API and (from spec 006) the Dome.
    struct AlpacaConfig
    {
        bool enabled; // master switch for the Alpaca HTTP+UDP endpoints
    };

    // Where the weather verdict comes from (spec 004): SQMeter over HTTP
    // and/or MQTT, plus the optional rain contact on GPIO4.
    struct SafetyConfig
    {
        bool httpEnabled = false;
        std::string httpUrl;               // SQMeter's GET /api/safety, http:// only
        bool mqttEnabled = false;          // on RoofRoof's own broker
        std::string mqttTopic = "sqmeter"; // SQMeter's base topic (<base>/safe, /safety, /availability)
        uint32_t staleSeconds = 30;        // SQMeter HTTP: unsafe after this long without a good reply
        bool rainContact = false;          // a rain contact is fitted on GPIO4
        static constexpr uint32_t MIN_STALE_SECONDS = 10;
        static constexpr uint32_t MAX_STALE_SECONDS = 600;
    };

    // How the park is confirmed (spec 004, constitution II): N.I.N.A. AND the
    // fitted park switches.
    struct ParkConfig
    {
        std::string ninaUrl; // N.I.N.A. Advanced API base, e.g. http://192.168.1.20:1888; empty = not configured
        enum class NinaApi : uint8_t
        {
            V2 = 0,
        };
        NinaApi ninaApi = NinaApi::V2;
        // Park switches fitted: none, one (GPIO32) or two (GPIO32 and GPIO33).
        enum class Switches : uint8_t
        {
            None = 0,
            One = 1,
            Two = 2,
        };
        Switches switches = Switches::None;
    };

    // Push notifications. Persisted under their own NVS keys (see
    // src/ConfigStore.cpp): ESP-IDF caps NVS strings at 4000 bytes.
    struct AlertsConfig
    {
        bool enabled; // master switch

        // Per event: how loudly to alert (0 off, 1 quiet, 2 normal, 3 urgent,
        // 4 wake me) and an optional Pushover sound.
        struct EventSetting
        {
            uint8_t level;
            std::string sound;
            // Custom text with {variables}; empty uses the built-in wording.
            std::string title;
            std::string message;
        };
        static constexpr size_t MAX_TEMPLATE_TITLE = 80;
        static constexpr size_t MAX_TEMPLATE_MESSAGE = 240;
        // Custom wording within the limits, counted in characters (as the
        // web UI counts them), not UTF-8 bytes.
        static bool templateFits(const std::string &title, const std::string &message);
        // The imaging app (any Alpaca client) stops checking a device, comes
        // back, or disconnects normally (SQMeter spec 021).
        EventSetting clientLost;
        EventSetting clientBack;
        EventSetting clientDisconnected;

        // When alerts are sent: any time (unless paused), or only while an
        // imaging app has an Alpaca device connected.
        enum class SendMode : uint8_t
        {
            Any = 0,
            WhileConnected = 1,
        };
        SendMode sendMode;
        // How long a device may go without a request before "the imaging app
        // stops checking" (seconds, 30-3600).
        uint32_t clientSilentSeconds;
        static constexpr uint32_t MIN_CLIENT_SILENT_SECONDS = 30;
        static constexpr uint32_t MAX_CLIENT_SILENT_SECONDS = 3600;
        uint32_t cooldownSeconds; // min time between notifications of the same kind

        // Channels
        bool pushoverEnabled;
        std::string pushoverUserKey;
        std::string pushoverAppToken;
        std::string pushoverSound; // default Pushover sound for events without their own

        bool ntfyEnabled;
        std::string ntfyServer; // e.g. https://ntfy.sh
        std::string ntfyTopic;
        std::string ntfyToken; // optional access token

        bool webhookEnabled;
        std::string webhookUrl;
        std::string webhookAuthHeader; // optional Authorization header value
        bool webhookInsecureTls;       // skip certificate checks (self-signed LAN servers only)

        bool mqttEnabled; // publish to <mqtt topic>/alerts
    };

    struct Config
    {
        WiFiConfig wifi;
        MQTTConfig mqtt;
        OTAConfig ota;
        AuthConfig auth;
        NTPConfig ntp;
        AlpacaConfig alpaca;
        AlertsConfig alerts;
        SafetyConfig safety;
        ParkConfig park;
        std::string deviceName;

        static constexpr const char *TAG = "Config";
        static constexpr size_t MAX_PERSISTED_JSON_BYTES = 5100;

        // Persistence (src/ConfigStore.cpp, firmware only).
        // Loads the stored config into `out`. False (with `out` unspecified)
        // if nothing valid is stored.
        static bool load(Config &out);
        bool save() const;
        static Config createDefault();

        std::string toJson(bool redactSecrets = false, bool includeAlerts = true) const;
        // The alerts settings as stored: Main under the "alerts" NVS key, Client
        // (the imaging-app events and silence time) under their own key so
        // each stays under the NVS string limit. All is what the API shows.
        enum class AlertsPart : uint8_t
        {
            All,
            Main,
            Client,
        };
        static constexpr size_t MAX_ALERTS_JSON_BYTES = 3900;
        // The stored document: the main JSON with each stored alerts part
        // spliced in ("alerts", "alertsClient"); an empty part is left out.
        // What load() parses (src/ConfigStore.cpp).
        static std::string spliceStored(const std::string &main, const std::string &alerts, const std::string &client);
        std::string alertsToJson(bool redactSecrets = false, AlertsPart part = AlertsPart::All) const;
        // `error` (if given) gets the reason a config is rejected - the same
        // message the web UI shows.
        bool validate(std::string *error = nullptr) const;
        static std::optional<Config> fromJson(const std::string &json, const Config *baseConfig = nullptr, std::string *error = nullptr);
        // Applies JSON on top of `cfg` in place and validates. On failure
        // `cfg` may be partially updated.
        static bool applyJson(const std::string &json, Config &cfg, bool preserveSecretPlaceholders, std::string *error = nullptr);
        // Settings > Network "Test connection" sends the form as shown, so an unchanged password
        // arrives as the mask. Returns the password to try: what was typed; or the saved one when
        // the form still shows the mask (or is blank) and names the saved broker, port and user.
        // A masked password for any other broker returns nothing (refused): the saved secret is
        // never sent to a host it wasn't saved for.
        struct MqttTestTarget
        {
            std::string broker;
            uint16_t port = 0;
            std::string username;
            std::string password;
        };
        static std::optional<std::string> mqttTestPassword(const MQTTConfig &saved, const MqttTestTarget &target);
    };

} // namespace SQM
