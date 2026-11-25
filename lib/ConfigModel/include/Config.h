#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "Analog.h"
#include "ConfigAutoClose.h"
#include "PinMap.h"

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

    // Spec 023: which network a board with Ethernet uses (a board without
    // Ethernet is always on Wi-Fi). Applied at boot.
    struct NetworkConfig
    {
        enum class Mode : uint8_t
        {
            Auto = 0,     // Ethernet; a saved Wi-Fi network once Ethernet has been down 30 s
            Ethernet = 1, // Ethernet only: the radio off, never a setup hotspot
            Wifi = 2,     // Wi-Fi only: Ethernet isn't started
        };
        Mode mode = Mode::Auto;
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
    // and/or MQTT. Spec 023 removed the local rain contact: rain comes from
    // SQMeter's rain gauge.
    struct SafetyConfig
    {
        bool httpEnabled = false;
        // SQMeter's address (spec 020 FR-036): scheme, host and port only
        // ("http://192.168.1.50", "https://sqmeter.lan:8443"); RoofRoof adds
        // /api/safety and the site paths itself. https is encrypted without a
        // certificate check (a LAN SQMeter's is self-signed).
        std::string httpUrl;
        // Spec 020: SQMeter's login (HTTP Basic), sent on every request to it:
        // the weather poll, the location and settings reads and the copy. Both
        // optional; the password is a secret (masked, cleared when the address
        // moves to another host or port unless a new one is typed with it).
        std::string httpUsername;
        std::string httpPassword;
        static constexpr size_t MAX_LOGIN_LENGTH = 64;
        bool mqttEnabled = false;          // on RoofRoof's own broker
        std::string mqttTopic = "sqmeter"; // SQMeter's base topic (<base>/safe, /safety, /availability)
        uint32_t staleSeconds = 30;        // SQMeter HTTP: unsafe after this long without a good reply
        static constexpr uint32_t MIN_STALE_SECONDS = 10;
        static constexpr uint32_t MAX_STALE_SECONDS = 600;
    };

    // How the park is confirmed (spec 004, constitution II): N.I.N.A. AND the
    // fitted park switches, or (spec 024) either alone.
    struct ParkConfig
    {
        // Spec 024: what confirms the park. Settings without it take it from
        // `switches` (none: Nina, one or two: NinaAndSwitches), today's rule.
        enum class Source : uint8_t
        {
            Nina = 0,
            Switches = 1,
            NinaAndSwitches = 2,
        };
        Source source = Source::Nina;
        std::string ninaUrl; // N.I.N.A. Advanced API base, e.g. http://192.168.1.20:1888; empty = not configured
        enum class NinaApi : uint8_t
        {
            V2 = 0,
        };
        NinaApi ninaApi = NinaApi::V2;
        // Park switches fitted: none, one (park switch A) or two (A and B); their pins are in `pins` (spec 023).
        enum class Switches : uint8_t
        {
            None = 0,
            One = 1,
            Two = 2,
        };
        Switches switches = Switches::None;
        bool switchesOnly() const { return source == Source::Switches; }
        // Spec 005: how long the close sequence waits for the park to be
        // confirmed before it raises "mount not parked". A fact of the mount.
        uint32_t timeoutSeconds = 240;
        static constexpr uint32_t MIN_TIMEOUT_SECONDS = 30;
        static constexpr uint32_t MAX_TIMEOUT_SECONDS = 1800;
        // Spec 022: the automatic close may ask N.I.N.A. to park the mount
        // when unsafe (Principle III step 2). On by default and after an
        // upgrade; the owner may turn it off (a bench box pointed at a live N.I.N.A.).
        bool requestPark = true;
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
        // Spec 013 daylight events (open, closed, check can't run), persisted with
        // the imaging-app events (the main part holds the long channel settings).
        EventSetting daylightOpen;
        EventSetting daylightClosed;
        EventSetting daylightUnknown;
        EventSetting ninaLost; // spec 027: N.I.N.A. can't be read
        // Spec 022: a test-box bypass came into effect, or is in effect after a
        // boot. Level 1 (quiet) to 4: never off (MIN_TEST_BOX_LEVEL).
        EventSetting testBox;
        static constexpr uint8_t MIN_TEST_BOX_LEVEL = 1;
        static constexpr uint8_t MAX_TEST_BOX_LEVEL = 3; // never wake me (a Pushover emergency)

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
        // At the slow duty, measured by a calibration run (0 = not measured), and the
        // slow duty it was measured at: used only while slowDutyPercent is still that.
        float slowSpeedMmPerSecond = 0.0f;
        uint32_t slowSpeedDutyPercent = 0;
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
        uint32_t calibrationLimitSeconds = 180; // spec 016: the longest a calibration leg may drive
        uint8_t measured = 0;                   // spec 026: Measured:: bits (RoofMeasured.h)

        // Spec 023: what the roof's gates compare the analog readings with.
        Board::AnalogGates analogGates() const;

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
        // Above the low rail (Roof::HALL_RAIL_LOW_COUNTS 400) and its hysteresis.
        static constexpr uint32_t MIN_HALL_COUNTS = 500;
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

    // Spec 023 review: whether the analog setup passes its checks against these
    // roof settings (errors only). Also what the roof applies, so stored values
    // that fail them (saved before a rule existed) refuse every move.
    bool analogUsable(const Board::AnalogConfig &analog, const RoofConfig &roof, std::string *error = nullptr);

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

    // Web UI preferences kept on the device (spec 014).
    struct UiConfig
    {
        bool confirmOpenClose = false; // Open and Close ask first; Stop never does
    };

    // The observing site, for the sun's altitude (spec 013; SQMeter's names,
    // without its GPS and sun-and-moon card). Spec 020: it can follow the
    // SQMeter at the same site; the device then keeps the last good location
    // here (and owns these fields while the source stays SQMeter).
    struct LocationConfig
    {
        enum class Source : uint8_t
        {
            Sqmeter = 0, // read from SQMeter's GET /api/status (sky) and kept here
            Manual = 1,  // typed in
        };
        bool set = false;
        double latitude = 0.0;  // degrees, north positive
        double longitude = 0.0; // degrees, east positive
        // A new device follows SQMeter; settings saved before spec 020 load as
        // Manual when a location was set (Config::applyJson).
        Source source = Source::Sqmeter;
        bool gps = false;     // the kept location was SQMeter's GPS fix (device-owned)
        uint32_t savedAt = 0; // Unix seconds it was saved from SQMeter; 0 = never or no clock (device-owned)
    };

    // Spec 020: which shared settings keep following the SQMeter at the same
    // site, re-read from its settings every 10 minutes. `syncedAt` is the
    // device's own (Unix seconds of the last change from SQMeter; 1 without a
    // clock; 0 never synced) and is never read from the API. A new device
    // follows in every group; settings saved before this load each group as
    // local when its values were already set (Config::applyJson).
    struct SqmeterSyncConfig
    {
        struct Group
        {
            bool follow = true;
            uint32_t syncedAt = 0;
        };
        Group time;   // ntp.timezone, ntp.server1, ntp.server2
        Group mqtt;   // mqtt.broker, mqtt.port, mqtt.username, safety.mqttTopic
        Group alerts; // alerts.ntfyServer, alerts.ntfyTopic
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
        uint8_t step = 0;                         // 0 to STEP_COUNT - 1
        static constexpr uint8_t STEP_COUNT = 12; // spec 016 added the calibration run page, spec 019 the teach page
    };

    // Advanced diagnostics (spec 017): the live document, the stream and the
    // move log. Off by default; the other two do nothing while it is off.
    // They observe only: nothing here changes how the roof moves.
    struct DiagConfig
    {
        bool enabled = false;
        bool keepSummaries = false; // the last 32 move summaries across restarts
        bool mqtt = false;          // <base>/diag and <base>/diag/move, at rest only
    };

    // Spec 022: test-box bypasses for a box with no telescope and no weather
    // station (constitution 3.0.0, Principles II and III). Off by default and
    // after an upgrade; set only by an authenticated settings save. They share
    // one deadline the device stamps (`until`, Unix seconds; never read from a
    // request or a restore). In effect only while the clock is set and before
    // `until` (lib/SafetyInputLogic TestBox).
    struct TestBoxConfig
    {
        bool park = false;     // "no telescope under this roof": stands in for N.I.N.A.
        bool weather = false;  // stands in for a missing weather source
        bool daylight = false; // no daylight close or open refusal
        uint32_t hours = 72;
        uint32_t until = 0;
        static constexpr uint32_t MIN_HOURS = 1;
        static constexpr uint32_t MAX_HOURS = 720;
        bool any() const { return park || weather || daylight; }
    };

    struct CopiedSecrets; // spec 024: CopiedSecrets.h

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
        SqmeterSyncConfig sqmeterSync;
        SetupConfig setup;
        DiagConfig diag;
        AutoCloseConfig autoClose;
        UiConfig ui;
        // Spec 023: the board and the roof's pins, applied at boot only. A new
        // device has none chosen (nothing driven); settings saved before spec
        // 023 load as the DevKit preset (Config::applyJson).
        Board::PinMap pins = Board::presetMap(Board::Preset::Unset);
        NetworkConfig network;
        // Spec 023: each analog input's divider and trim (Advanced). Applied by
        // the roof once no move is under way; the default is the DevKit's.
        Board::AnalogConfig analog;
        TestBoxConfig testBox;
        std::string deviceName;

        static constexpr const char *TAG = "Config";
        static constexpr size_t MAX_PERSISTED_JSON_BYTES = 5100;

        // Persistence (src/ConfigStore.cpp, firmware only).
        // Loads the stored config into `out`. False (with `out` unspecified)
        // if nothing valid is stored. `storedRejected` (spec 023): settings
        // were stored but couldn't be read, so their pins are unknown.
        static bool load(Config &out, bool *storedRejected = nullptr);
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
        // Spec 023: the pins against the rules for a chip with or without
        // PSRAM (validate() assumes none; the device checks its own chip too).
        bool validateAnalog(std::string *error = nullptr) const;
        // What the roof's gates compare the analog readings with (spec 023):
        // a divider or trim is checked against them, so it can't defeat one.
        Board::AnalogGates analogGates() const;
        // `requireWt32`: the device has seen a LAN8720, so only the WT32-ETH01 board is allowed.
        bool validatePins(bool psram, std::string *error = nullptr, bool requireWt32 = false) const;
        // validate() plus the rules a change from the UI/API also meets (host and
        // URL forms, Pushover key format): what a change synced from SQMeter must pass (spec 020).
        bool validateChange(std::string *error = nullptr) const;
        // A secret sent back as shown (the mask, or empty): "keep the saved one".
        static bool placeholderSecret(const std::string &value);
        // Spec 015: a stored document with no "setup" object was saved by a
        // release before the setup wizard; Config::load then marks it offered.
        static bool storedBeforeSetup(const std::string &storedJson);
        void markStoredBeforeSetup();
        static std::optional<Config> fromJson(const std::string &json, const Config *baseConfig = nullptr, std::string *error = nullptr);
        // Spec 020 FR-036: SQMeter's address as typed ("192.168.1.50",
        // "sqmeter.local", "http://sqm/api/sensors") as it is kept:
        // scheme://host[:port]. No scheme means http://; a known SQMeter API
        // path (/api/safety, /api/sensors, /api/status, /api/config), a query
        // and a trailing "/" are dropped. Any other path stays (a save refuses
        // it) unless `anyPath` (stored settings: dropped). Empty stays empty.
        static std::string sqmeterAddress(const std::string &typed, bool anyPath = false);
        // Applies JSON on top of `cfg` in place and validates. On failure
        // `cfg` may be partially updated. Spec 024: from the UI/API, a secret
        // sent as COPIED_SECRET takes its value from `copied` (the held copy
        // from SQMeter, nullptr when none is held), only while the field's
        // server is the one it was read for; otherwise the save is refused.
        static bool applyJson(
            const std::string &json,
            Config &cfg,
            bool preserveSecretPlaceholders,
            std::string *error = nullptr,
            const CopiedSecrets *copied = nullptr);
        // "(from SQMeter)": a secret to take from the copy, never stored.
        static constexpr const char *COPIED_SECRET = "(from SQMeter)";
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
