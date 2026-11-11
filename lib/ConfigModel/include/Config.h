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
        // Open, close and stop from <topic>/roof/set (spec 008). Off by default,
        // and off for config saved before it existed (a migration never turns
        // on a motion feature).
        bool roofCommands = false;
        // Spec 014: at rest, a change of the roof's state is published at most
        // this often (seconds). While the roof is busy spec 008's fixed cadence applies.
        uint32_t changeMinSeconds = 1;
        static constexpr uint32_t MIN_CHANGE_MIN_SECONDS = 1;
        static constexpr uint32_t MAX_CHANGE_MIN_SECONDS = 60;
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
        // Spec 005: how long the close sequence waits for the park to be
        // confirmed before it raises "mount not parked". A fact of the mount.
        uint32_t timeoutSeconds = 240;
        static constexpr uint32_t MIN_TIMEOUT_SECONDS = 30;
        static constexpr uint32_t MAX_TIMEOUT_SECONDS = 1800;
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
        // The roof (spec 007). The first two are critical: their level is 3
        // (urgent) or 4 (wake me), never lower.
        static constexpr uint8_t MIN_CRITICAL_LEVEL = 3;
        // A Pushover sound that makes a noise: not "none" (silent) or
        // "vibrate"; the critical events can't use those.
        static bool audibleSound(const std::string &sound) { return sound != "none" && sound != "vibrate"; }
        // Settings read back from NVS: a critical event below urgent, or
        // silent, is raised to urgent with the default sound rather than
        // refused, so its channels aren't lost with it.
        void raiseCriticalToMinimum();
        EventSetting mountNotParked;
        EventSetting roofNotClosed;
        EventSetting moveFault;
        EventSetting supplyLow;
        EventSetting unsafeClosing;
        EventSetting roofClosed;
        EventSetting safetyLost;
        // The imaging app (any Alpaca client) stops checking a device, comes
        // back, or disconnects normally (SQMeter spec 021).
        EventSetting clientLost;
        EventSetting clientBack;
        EventSetting clientDisconnected;
        // The daylight failsafe (spec 013): the roof open in daylight, closed
        // after a daylight close, and the check unable to run. Persisted with
        // the imaging-app events (the main part holds the long channel settings).
        EventSetting daylightOpen;
        EventSetting daylightClosed;
        EventSetting daylightUnknown;

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

    // How the roof moves (spec 002, data-model.md). The roof's measured facts and the
    // motion profile; lib/RoofLogic turns them into Roof::Settings. The defaults
    // are conservative until the roof is measured (hardware facts are
    // settings), and current sense starts off (constitution: a migration never
    // turns a motion feature on).
    struct RoofConfig
    {
        // Which driver direction (RPWM = forward, LPWM = reverse) opens the roof.
        enum class Direction : uint8_t
        {
            Forward = 0,
            Reverse = 1,
        };
        // Which end's magnet reads high on the Hall sensor (the magnets have opposite poles).
        enum class HallEnd : uint8_t
        {
            Open = 0,
            Closed = 1,
        };

        uint32_t travelMm = 1850;
        float speedMmPerSecond = 38.0f; // at 100% duty
        uint32_t timeoutMarginSeconds = 15;
        uint32_t leaveMagnetSeconds = 10;
        uint32_t rampUpMs = 2000;
        uint32_t rampDownMs = 3000;
        uint32_t topDutyPercent = 100;
        uint32_t slowDutyPercent = 40; // the ends, and every move from an unknown position
        Direction openDirection = Direction::Forward;
        HallEnd hallHighEnd = HallEnd::Open;
        uint32_t hallHighCounts = 3000; // raw ADC counts: at or above is the high end's magnet
        uint32_t hallLowCounts = 1100;  // at or below is the other end's magnet
        bool currentSenseEnabled = false;
        float maxCurrentAmps = 10.0f;
        // Motor amps per volt at the IBT-2's IS output (before the divider): the
        // BTS7960's sense ratio into the board's sense resistor (spec 003).
        float currentAmpsPerVolt = 8.5f;
        // With current sense on: a running current below this, after a second
        // of driving, counts as a broken sense wire. 0 = off (spec 003; set it
        // from a bench measurement, the ADC can't resolve the lowest currents).
        float minRunningAmps = 0.0f;
        float minSupplyVolts = 11.0f;
        // Spec 016: the longest a calibration leg may drive (its hard timeout).
        uint32_t calibrationLimitSeconds = 180;

        // The ranges, shared with the web UI (test/fixtures/config-ranges.json) and
        // checked again by Roof::validSettings.
        static constexpr uint32_t MIN_TRAVEL_MM = 100;
        static constexpr uint32_t MAX_TRAVEL_MM = 20000;
        static constexpr float MIN_SPEED_MM_PER_SECOND = 1.0f;
        static constexpr float MAX_SPEED_MM_PER_SECOND = 500.0f;
        static constexpr uint32_t MIN_TIMEOUT_MARGIN_SECONDS = 3;
        static constexpr uint32_t MAX_TIMEOUT_MARGIN_SECONDS = 300;
        static constexpr uint32_t MIN_LEAVE_MAGNET_SECONDS = 2;
        static constexpr uint32_t MAX_LEAVE_MAGNET_SECONDS = 120;
        static constexpr uint32_t MIN_CALIBRATION_LIMIT_SECONDS = 30;
        static constexpr uint32_t MAX_CALIBRATION_LIMIT_SECONDS = 600;
        static constexpr uint32_t MAX_RAMP_MS = 30000;
        static constexpr uint32_t MIN_TOP_DUTY_PERCENT = 20;
        static constexpr uint32_t MIN_SLOW_DUTY_PERCENT = 10;
        static constexpr uint32_t MAX_DUTY_PERCENT = 100;
        static constexpr uint32_t MIN_HALL_COUNTS = 100;
        static constexpr uint32_t MAX_HALL_COUNTS = 3995;
        // Two hysteresis bands (Roof::HALL_HYSTERESIS_COUNTS), so leaving one
        // magnet's zone can never land in the other's.
        static constexpr uint32_t MIN_HALL_GAP_COUNTS = 80;
        static constexpr float MIN_CURRENT_AMPS = 0.5f;
        static constexpr float MAX_CURRENT_AMPS = 40.0f;
        static constexpr float MIN_CURRENT_AMPS_PER_VOLT = 0.5f;
        static constexpr float MAX_CURRENT_AMPS_PER_VOLT = 100.0f;
        static constexpr float MAX_MIN_RUNNING_AMPS = 20.0f;
        static constexpr float MIN_SUPPLY_VOLTS = 6.0f;
        static constexpr float MAX_SUPPLY_VOLTS = 15.0f;
    };

    // The daylight failsafe (spec 013): with the sun at or above
    // sunAltitudeDeg, alert (and with Close, close the roof as for unsafe
    // weather and refuse opening). Alert is the default and what an older
    // config loads as: a migration never turns a motion feature on.
    struct DaylightConfig
    {
        enum class Mode : uint8_t
        {
            Off = 0,
            Alert = 1,
            Close = 2, // "alert and close"
        };
        Mode mode = Mode::Alert;
        float sunAltitudeDeg = 0.0f; // the sun's centre, degrees above the horizon
        static constexpr float MIN_SUN_ALTITUDE_DEG = -18.0f;
        static constexpr float MAX_SUN_ALTITUDE_DEG = 30.0f;
        // Spec 014: once day, night again only this far below the threshold.
        float bandDeg = 0.5f;
        static constexpr float MIN_BAND_DEG = 0.1f;
        static constexpr float MAX_BAND_DEG = 2.0f;
    };

    // The automatic close's policy (spec 014; specs 005 and 013 fixed these).
    // The defaults are the values those specs chose; lib/CloseSequenceLogic's
    // ClosePolicy clamps them again.
    struct AutoCloseConfig
    {
        uint32_t rearmMinutes = 10;       // the safe spell that ends a stand-down
        uint32_t parkRequestSeconds = 30; // between park requests
        uint32_t parkHoldSeconds = 4;     // the park must hold this long before the close
        uint32_t retryMinutes = 5;        // the back-off after a failed attempt or close
        uint32_t closeStartSeconds = 30;  // a waiting close must start within this
        uint32_t closeGuardSeconds = 30;  // added to the move's own timeout
        enum class NotStarted : uint8_t
        {
            Halt = 0,
            Retry = 1,
        };
        NotStarted notStarted = NotStarted::Halt;
        uint32_t remindMinutes = 30; // critical reminders and their throttle
        // An Alpaca or MQTT stop of a close in unsafe weather counts as a
        // person's stop (stand-down and its critical alert) instead of being refused.
        bool remoteStop = false;

        static constexpr uint32_t MIN_REARM_MINUTES = 5;
        static constexpr uint32_t MAX_REARM_MINUTES = 60;
        static constexpr uint32_t MIN_PARK_REQUEST_SECONDS = 10;
        static constexpr uint32_t MAX_PARK_REQUEST_SECONDS = 120;
        static constexpr uint32_t MIN_PARK_HOLD_SECONDS = 4;
        static constexpr uint32_t MAX_PARK_HOLD_SECONDS = 30;
        static constexpr uint32_t MIN_RETRY_MINUTES = 1;
        static constexpr uint32_t MAX_RETRY_MINUTES = 30;
        static constexpr uint32_t MIN_CLOSE_START_SECONDS = 25;
        static constexpr uint32_t MAX_CLOSE_START_SECONDS = 120;
        static constexpr uint32_t MIN_CLOSE_GUARD_SECONDS = 10;
        static constexpr uint32_t MAX_CLOSE_GUARD_SECONDS = 120;
        static constexpr uint32_t MIN_REMIND_MINUTES = 10;
        static constexpr uint32_t MAX_REMIND_MINUTES = 120;
    };

    // Web UI preferences kept on the device (spec 014).
    struct UiConfig
    {
        bool confirmOpenClose = false; // Open and Close ask first; Stop never does
    };

    // The observing site, for the sun's altitude (spec 013; SQMeter's names,
    // without its GPS and sun-and-moon card).
    struct LocationConfig
    {
        bool set = false;
        double latitude = 0.0;  // degrees, north positive
        double longitude = 0.0; // degrees, east positive
    };

    // The setup wizard (spec 015): whether it still has to be offered, and
    // the page to resume at. New for a device with no stored settings;
    // Offered for settings saved before this existed (an upgraded device gets
    // a banner, never the wizard by itself). Moves nothing, needs no restart.
    struct SetupConfig
    {
        enum class State : uint8_t
        {
            New = 0,
            Offered = 1,
            Done = 2,
        };
        State state = State::New;
        uint8_t step = 0; // 0 to STEP_COUNT - 1
        static constexpr uint8_t STEP_COUNT = 11; // spec 016 added the calibration run page
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
        RoofConfig roof;
        DaylightConfig daylight;
        LocationConfig location;
        SetupConfig setup;
        AutoCloseConfig autoClose;
        UiConfig ui;
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
        // validate() without the stored-size check, for settings read back
        // from NVS (spec 007: an upgrade that adds events must still load).
        bool validateLoaded(std::string *error = nullptr) const;
        // Spec 015: a stored document with no "setup" object was saved by a
        // release before the setup wizard; Config::load then marks it offered.
        static bool storedBeforeSetup(const std::string &storedJson);
        void markStoredBeforeSetup();
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
