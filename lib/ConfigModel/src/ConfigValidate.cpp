#include "Config.h"
#include "ConfigInternal.h"
#include "NetAddress.h"
#include <cctype>
#include <cmath>
#include <initializer_list>

// Config::validate(): every rule the device enforces, one function per
// settings area, checked in a fixed order so the first problem found is the
// one reported. The web UI applies the same rules
// (web/src/validation/configSchema.ts, test/fixtures/config-ranges.json).

namespace SQM
{
    using namespace ConfigDetail;

    namespace
    {
        // Letters, digits, _ and -, with / between levels (same rule as the web UI).
        bool validTopic(const std::string &topic)
        {
            if (topic.empty() || topic.front() == '/' || topic.back() == '/' || topic.find("//") != std::string::npos)
                return false;
            for (char c : topic)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/'))
                    return false;
            return true;
        }

        // Characters, not bytes: the web UI counts what people type, and one
        // Japanese or Arabic character is two or three bytes in UTF-8.
        size_t utf8Length(const std::string &text)
        {
            size_t count = 0;
            for (char c : text)
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) // not a continuation byte
                    ++count;
            return count;
        }

        // A DNS label, so it works as <hostname>.local (same rule as the web UI).
        bool validHostname(const std::string &name)
        {
            if (name.empty() || name.size() > 32 || name.front() == '-' || name.back() == '-')
                return false;
            for (char c : name)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-'))
                    return false;
            return true;
        }

        bool isHttpUrl(const std::string &url)
        {
            return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
        }

        bool validateIdentity(const Config &c, std::string *error)
        {
            if (c.deviceName.empty())
                return setError(error, "Device name is required");
            return true;
        }

        bool validReconnectDelays(const WiFiConfig &w)
        {
            constexpr uint32_t DAY_MS = 86400000;
            return w.reconnectDelayMs > 0 && w.maxReconnectDelayMs > 0 && w.reconnectDelayMs <= DAY_MS && w.maxReconnectDelayMs <= DAY_MS &&
                   w.reconnectDelayMs <= w.maxReconnectDelayMs;
        }

        bool validateWifiAndMqtt(const Config &c, std::string *error)
        {
            const WiFiConfig &w = c.wifi;
            if (!validReconnectDelays(w))
                return setError(error, "WiFi reconnect delays are invalid");
            if (c.mqtt.port == 0)
                return setError(error, "MQTT port is invalid");
            if (c.mqtt.enabled && (c.mqtt.broker.empty() || c.mqtt.topic.empty()))
                return setError(error, "MQTT broker and topic are required when MQTT is enabled");
            if (!validHostname(w.hostname))
                return setError(error, "Hostname: use up to 32 letters, numbers and hyphens (not at either end)");
            if (c.mqtt.enabled && !validTopic(c.mqtt.topic))
                return setError(error, "MQTT topic: use letters, numbers, _ and -, with / between levels");
            if (c.mqtt.homeAssistant && !validTopic(c.mqtt.discoveryPrefix))
                return setError(error, "MQTT discovery prefix: use letters, numbers, _ and -, with / between levels");
            if (c.mqtt.publishIntervalMs < 1000 || c.mqtt.publishIntervalMs > 86400000)
                return setError(error, "MQTT publish interval is invalid");
            return true;
        }

        bool validateAuthAndNtp(const Config &c, std::string *error)
        {
            if (c.auth.enabled && c.auth.password.empty())
                return setError(error, "HTTP auth password is required when auth is enabled");
            if (c.auth.enabled && c.auth.username.empty())
                return setError(error, "HTTP auth username is required when auth is enabled");
            if (c.ntp.enabled && c.ntp.server1.empty())
                return setError(error, "Primary NTP server is required when NTP is enabled");
            if (c.ntp.syncIntervalMs < 600000 || c.ntp.syncIntervalMs > 86400000)
                return setError(error, "NTP sync interval is invalid");
            return true;
        }

        bool validateAlertThresholds(const Config &c, std::string *error)
        {
            if (c.alerts.cooldownSeconds > 86400)
                return setError(error, "Alerts: cooldown must be between 0 and 86400 seconds");
            return true;
        }

        bool validateAlertEvents(const Config &c, std::string *error)
        {
            const AlertsConfig &a = c.alerts;
            for (const auto &entry : eventSettings(a))
            {
                if (entry.second->level > 4)
                    return setError(error, "Alerts: event levels are 0 (off) to 4 (wake me)");
                if (!AlertsConfig::templateFits(entry.second->title, entry.second->message))
                    return setError(error, "Alerts: custom titles are up to 80 characters and messages up to 240");
            }
            if (a.sendMode != AlertsConfig::SendMode::Any && a.sendMode != AlertsConfig::SendMode::WhileConnected)
                return setError(error, "Alerts: when to send must be any or whileConnected");
            if (a.clientSilentSeconds < AlertsConfig::MIN_CLIENT_SILENT_SECONDS ||
                a.clientSilentSeconds > AlertsConfig::MAX_CLIENT_SILENT_SECONDS)
                return setError(error, "Alerts: silence time must be between 30 and 3600 seconds");
            // NVS strings top out just under 4000 bytes; each stored part has its own key.
            if (c.alertsToJson(false, Config::AlertsPart::Main).size() > Config::MAX_ALERTS_JSON_BYTES ||
                c.alertsToJson(false, Config::AlertsPart::Client).size() > Config::MAX_ALERTS_JSON_BYTES)
                return setError(error, "Alerts: the custom alert texts are too long in total - shorten some");
            return true;
        }

        // SQMeter and N.I.N.A. serve plain http only (spec 004 research D9):
        // https would also take the alert task's TLS session.
        bool validateSafety(const Config &c, std::string *error)
        {
            const SafetyConfig &s = c.safety;
            if (s.httpEnabled && s.httpUrl.rfind("http://", 0) != 0)
                return setError(error, "SQMeter safety URL must start with http://");
            if (s.mqttEnabled && !validTopic(s.mqttTopic))
                return setError(error, "SQMeter MQTT topic: use letters, numbers, _ and -, with / between levels");
            if (s.staleSeconds < SafetyConfig::MIN_STALE_SECONDS || s.staleSeconds > SafetyConfig::MAX_STALE_SECONDS)
                return setError(error, "Safety: stale time must be between 10 and 600 seconds");
            if (!c.park.ninaUrl.empty() && c.park.ninaUrl.rfind("http://", 0) != 0)
                return setError(error, "N.I.N.A. URL must start with http://");
            return true;
        }

        bool validateAlertChannels(const AlertsConfig &a, std::string *error)
        {
            if (a.pushoverEnabled && (a.pushoverUserKey.empty() || a.pushoverAppToken.empty()))
                return setError(error, "Alerts: Pushover needs both a user key and an application token");
            if (a.ntfyEnabled && (!isHttpUrl(a.ntfyServer) || a.ntfyTopic.empty()))
                return setError(error, "Alerts: ntfy needs an http(s):// server and a topic");
            if (a.webhookEnabled && !isHttpUrl(a.webhookUrl))
                return setError(error, "Alerts: webhook URL must start with http:// or https://");
            return true;
        }

        bool validateRoofTiming(const RoofConfig &r, std::string *error)
        {
            if (r.travelMm < RoofConfig::MIN_TRAVEL_MM || r.travelMm > RoofConfig::MAX_TRAVEL_MM)
                return setError(error, "Roof: travel must be between 100 and 20000 mm");
            if (!inRange(r.speedMmPerSecond, RoofConfig::MIN_SPEED_MM_PER_SECOND, RoofConfig::MAX_SPEED_MM_PER_SECOND))
                return setError(error, "Roof: speed must be between 1 and 500 mm/s");
            if (r.timeoutMarginSeconds < RoofConfig::MIN_TIMEOUT_MARGIN_SECONDS ||
                r.timeoutMarginSeconds > RoofConfig::MAX_TIMEOUT_MARGIN_SECONDS)
                return setError(error, "Roof: timeout margin must be between 3 and 300 seconds");
            if (r.leaveMagnetSeconds < RoofConfig::MIN_LEAVE_MAGNET_SECONDS || r.leaveMagnetSeconds > RoofConfig::MAX_LEAVE_MAGNET_SECONDS)
                return setError(error, "Roof: leave-magnet time must be between 2 and 120 seconds");
            if (r.rampUpMs > RoofConfig::MAX_RAMP_MS)
                return setError(error, "Roof: ramp up must be between 0 and 30000 ms");
            if (r.rampDownMs > RoofConfig::MAX_RAMP_MS)
                return setError(error, "Roof: ramp down must be between 0 and 30000 ms");
            return true;
        }

        bool validateRoofDuty(const RoofConfig &r, std::string *error)
        {
            if (r.topDutyPercent < RoofConfig::MIN_TOP_DUTY_PERCENT || r.topDutyPercent > RoofConfig::MAX_DUTY_PERCENT)
                return setError(error, "Roof: top duty must be between 20 and 100 percent");
            if (r.slowDutyPercent < RoofConfig::MIN_SLOW_DUTY_PERCENT || r.slowDutyPercent > RoofConfig::MAX_DUTY_PERCENT)
                return setError(error, "Roof: slow duty must be between 10 and 100 percent");
            if (r.slowDutyPercent > r.topDutyPercent)
                return setError(error, "Roof: slow duty can't be above the top duty");
            return true;
        }

        bool validateRoofSensing(const RoofConfig &r, std::string *error)
        {
            if (r.hallHighCounts < RoofConfig::MIN_HALL_COUNTS || r.hallHighCounts > RoofConfig::MAX_HALL_COUNTS)
                return setError(error, "Roof: Hall high threshold must be between 100 and 3995 counts");
            if (r.hallLowCounts < RoofConfig::MIN_HALL_COUNTS || r.hallLowCounts > RoofConfig::MAX_HALL_COUNTS)
                return setError(error, "Roof: Hall low threshold must be between 100 and 3995 counts");
            if (r.hallHighCounts < r.hallLowCounts + RoofConfig::MIN_HALL_GAP_COUNTS)
                return setError(error, "Roof: the Hall high threshold must be at least 80 counts above the low threshold");
            if (!inRange(r.maxCurrentAmps, RoofConfig::MIN_CURRENT_AMPS, RoofConfig::MAX_CURRENT_AMPS))
                return setError(error, "Roof: current limit must be between 0.5 and 40 amps");
            if (!inRange(r.minSupplyVolts, RoofConfig::MIN_SUPPLY_VOLTS, RoofConfig::MAX_SUPPLY_VOLTS))
                return setError(error, "Roof: minimum supply must be between 6 and 15 volts");
            return true;
        }

        bool validateRoof(const Config &c, std::string *error)
        {
            return validateRoofTiming(c.roof, error) && validateRoofDuty(c.roof, error) && validateRoofSensing(c.roof, error);
        }
    } // namespace

    bool AlertsConfig::templateFits(const std::string &title, const std::string &message)
    {
        return utf8Length(title) <= MAX_TEMPLATE_TITLE && utf8Length(message) <= MAX_TEMPLATE_MESSAGE;
    }

    namespace ConfigDetail
    {
        bool validateAddresses(const Config &cfg, std::string *error)
        {
            if (!cfg.mqtt.broker.empty())
            {
                Net::Host host;
                const Net::HostError hostError = Net::parseHost(cfg.mqtt.broker, host);
                if (hostError != Net::HostError::None)
                    return setError(error, std::string("MQTT broker: ") + Net::hostErrorText(hostError));
            }
            const std::pair<bool, std::pair<const char *, const std::string *>> urls[] = {
                {cfg.alerts.webhookEnabled, {"Alerts: webhook URL: ", &cfg.alerts.webhookUrl}},
                {cfg.alerts.ntfyEnabled, {"Alerts: ntfy server: ", &cfg.alerts.ntfyServer}},
                {cfg.safety.httpEnabled, {"SQMeter safety URL: ", &cfg.safety.httpUrl}},
                {!cfg.park.ninaUrl.empty(), {"N.I.N.A. URL: ", &cfg.park.ninaUrl}},
            };
            for (const auto &entry : urls)
            {
                if (!entry.first)
                    continue;
                Net::HttpUrl url;
                Net::HostError hostError = Net::HostError::None;
                const Net::UrlError urlError = Net::parseHttpUrl(*entry.second.second, url, &hostError);
                if (urlError != Net::UrlError::None)
                    return setError(error, entry.second.first + Net::urlErrorText(urlError, hostError));
            }
            for (const std::string *server : {&cfg.ntp.server1, &cfg.ntp.server2})
            {
                if (Net::isIpv6Literal(*server))
                    return setError(error, std::string("NTP server: ") + Net::NTP_IPV6_TEXT);
            }
            return true;
        }
    } // namespace ConfigDetail

    bool Config::validate(std::string *error) const
    {
        using Check = bool (*)(const Config &, std::string *);
        static const Check CHECKS[] = {
            validateIdentity,
            validateWifiAndMqtt,
            validateAuthAndNtp,
            validateAlertThresholds,
            validateRoof,
            validateAlertEvents,
            [](const Config &c, std::string *e) { return validateAlertChannels(c.alerts, e); },
            validateSafety,
        };
        for (Check check : CHECKS)
            if (!check(*this, error))
                return false;
        return true;
    }
} // namespace SQM
