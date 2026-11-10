#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace SQM
{
    namespace Alerts
    {

        enum class AlertType : uint8_t
        {
            AlertsOn, // alerts resumed
            Test,
            // The imaging app (any Alpaca client) stopped checking a device,
            // came back, or disconnected normally (SQMeter spec 021).
            ClientLost,
            ClientBack,
            ClientDisconnected,
        };
        // Keep in step with the enum: alertTypeName() is a table indexed by it.
        constexpr size_t ALERT_TYPE_COUNT = static_cast<size_t>(AlertType::ClientDisconnected) + 1;

        // How loudly an alert is delivered; set per event in the alert
        // settings. The dispatcher maps it to each channel's priority.
        enum class AlertLevel : uint8_t
        {
            Off = 0,
            Quiet = 1,
            Normal = 2,
            Urgent = 3,
            Wake = 4,
        };

        struct Alert
        {
            AlertType type = AlertType::Test;
            AlertLevel level = AlertLevel::Normal;
            std::string title;
            std::string message;
            std::string sound; // Pushover sound; empty = channel default
            // Values for message templates ({name} -> value), e.g. "device",
            // "silent_for". The firmware adds the device's own values.
            std::vector<std::pair<std::string, std::string>> vars;
            // Other events sent in this same notification (see stackAlerts).
            std::vector<AlertType> stacked;
        };

        const char *alertTypeName(AlertType type);
        const char *alertLevelName(AlertLevel level);

        // How each level is delivered. Pushover: quiet -1 (no sound), normal
        // 0, urgent 1 (bypasses quiet hours), wake 2 (emergency: repeats until
        // acknowledged, so it needs retry/expire). ntfy: low/default/high/max.
        int pushoverPriority(AlertLevel level);
        const char *ntfyPriority(AlertLevel level);

        struct AlertRules
        {
            uint32_t cooldownSeconds = 300;
            // Nothing is sent this soon after boot; state is still tracked,
            // so a reboot doesn't announce whatever the startup state is.
            // Imaging-app events don't wait for it: they need a request
            // since the restart anyway.
            uint32_t startupGraceSeconds = 60;
            bool onClientLost = true;
            bool onClientBack = true;
            bool onClientDisconnected = false;
        };

        // The most Alpaca devices an imaging app can be watched on. Spec 006
        // adds the Dome; there are none before it.
        constexpr size_t MAX_CLIENT_DEVICES = 2;

        // One Alpaca device as the imaging app sees it (from Alpaca::ClientWatch).
        struct ClientInputs
        {
            const char *key = "";         // the device's type key, e.g. "dome"
            const char *device = "";      // its name in alert text, e.g. "roof"
            bool watching = false;        // a session since the restart
            bool silent = false;          // no request for the silence time
            bool disconnectedNow = false; // disconnected cleanly since the last pass
            std::string silentFor;        // the silence time, e.g. "2 min"
            std::string lastChecked;      // "21:04", or "3 min ago" without a clock
            std::string clientId;         // last Alpaca ClientID, or ""
        };

        struct AlertInputs
        {
            uint32_t nowSeconds = 0; // monotonic
            size_t clientCount = 0;  // devices in use in `clients`
            ClientInputs clients[MAX_CLIENT_DEVICES];
        };

        // Why a change hasn't been announced yet.
        enum class WaitKind : uint8_t
        {
            Grace,    // nothing is sent this soon after boot
            Settle,   // the new state must hold this long first
            Cooldown, // the last notice for this condition was too recent
        };

        struct Wait
        {
            std::string condition; // "startup", "client:<key>"
            WaitKind kind = WaitKind::Settle;
            uint32_t remainingSeconds = 0;
        };

        const char *waitKindName(WaitKind kind);

        // Edge-triggered, rate-limited event detection. Each condition is a
        // tracked boolean; a notification is emitted when its value differs
        // from the last *notified* value and the per-condition cooldown has
        // passed. A change suppressed by the cooldown is not lost - it's
        // sent once the cooldown ends if the condition still differs, so the
        // last notification always matches reality.
        class AlertEngine
        {
        public:
            std::vector<Alert> update(const AlertInputs &inputs, const AlertRules &rules);

            // Changes seen but not announced yet, and how long until they
            // can be (as of the last update()). Doesn't change any state.
            std::vector<Wait> waits(uint32_t nowSeconds, const AlertRules &rules) const;

        private:
            struct Tracker
            {
                bool initialized = false;
                bool notified = false;
                bool hasNotified = false;
                uint32_t lastNotifiedAt = 0;
                bool pending = false; // value differs from notified, waiting to settle
                uint32_t pendingSince = 0;
            };

            // When a change may be announced: the time, the cooldown since the
            // last announcement, whether announcing is allowed at all, and
            // `settle`, the seconds a new value must hold before it counts.
            struct Emit
            {
                uint32_t now;
                uint32_t cooldown;
                bool allowed;
                uint32_t settle = 0;
            };

            // Returns true when a transition to `current` should be emitted now.
            // A tracker that follows the condition silently (before its first
            // update, during the startup grace, while its rule is off) takes the
            // current value, or with `announceAtStart` the all-clear value, so a
            // condition already present then (a battery already low at power-on)
            // is announced once announcing is allowed.
            static bool sync(Tracker &tracker, bool current, const Emit &emit, bool announceAtStart = false);

            bool started = false;
            uint32_t startedAt = 0;

            // Imaging app: notified value = "lost"; disconnects wait out the
            // cooldown like any other change.
            Tracker clients[MAX_CLIENT_DEVICES];
            std::string clientKeys[MAX_CLIENT_DEVICES]; // for waits()
            bool lostAnnounced[MAX_CLIENT_DEVICES] = {}; // a "stopped checking" awaiting its "is back"
            bool disconnectPending[MAX_CLIENT_DEVICES] = {};
            bool disconnectSent = false;
            uint32_t disconnectSentAt = 0;

            enum class ClientChange
            {
                None,
                Lost,
                Back
            };
            ClientChange clientSilence(size_t index, bool silent, bool allowed, uint32_t now, uint32_t cooldown);
            void updateClients(const AlertInputs &inputs, const AlertRules &rules, std::vector<Alert> &alerts);
        };

        // Formatting helpers shared with the dispatcher's test alerts.
        // One reason per line, bulleted.
        std::string joinReasons(const std::vector<std::string> &reasons);

        // Replaces each {name} in `text` with its value from `vars`. Unknown
        // names are left as they are, so a typo shows up in the alert.
        std::string renderTemplate(const std::string &text, const std::vector<std::pair<std::string, std::string>> &vars);

        // Several alerts raised at once become one notification: the
        // loudest one leads (its type, level and sound), titles are joined
        // with " · " and messages one after another.
        Alert stackAlerts(const std::vector<Alert> &alerts);

    } // namespace Alerts
} // namespace SQM
