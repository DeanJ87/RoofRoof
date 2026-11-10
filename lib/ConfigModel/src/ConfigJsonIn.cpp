#include "Config.h"
#include "ConfigInternal.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cstring>

// Settings as JSON, in: applyJson(). One function per section; a key that's
// missing leaves the setting as it was, and a value of the wrong type is
// never swapped for a default (JsonReader in ConfigInternal.h).

namespace SQM
{
    using namespace ConfigDetail;

    namespace
    {
        bool isPlaceholderSecret(const char *value)
        {
            return value == nullptr || value[0] == '\0' || std::strcmp(value, SECRET_MASK) == 0 || std::strcmp(value, "***") == 0;
        }

        // A secret: null clears it; from the UI/API the mask (or "") keeps it.
        void assignSecret(const Section &obj, const char *key, std::string &target, bool preservePlaceholders)
        {
            if (!obj.has(key))
                return;
            JsonVariant value = obj[key];
            if (value.isNull())
            {
                target.clear();
                return;
            }
            if (!obj.check<std::string>(key))
                return;
            const char *secret = value.as<const char *>();
            if (preservePlaceholders && isPlaceholderSecret(secret))
                return;
            target = secret;
        }

        bool isBlank(const std::string &value)
        {
            return value.find_first_not_of(" \t\r\n") == std::string::npos;
        }

        void trimInPlace(std::string &value)
        {
            const size_t start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
            {
                value.clear();
                return;
            }
            value = value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
        }

        // Settings stored as unsigned integers. ArduinoJson turns -1 or 1.5
        // into the default instead of failing, so reject them up front, as
        // the web UI does (test/fixtures/config-ranges.json).
        bool checkWholeNumbers(const JsonDocument &doc, std::string *error)
        {
            static const char *const FIELDS[][2] = {
                {"wifi", "reconnectDelayMs"},
                {"wifi", "maxReconnectDelayMs"},
                {"mqtt", "port"},
                {"mqtt", "publishIntervalMs"},
                {"ntp", "syncIntervalMs"},
                {"alerts", "cooldownSeconds"},
                {"alerts", "clientSilentSeconds"},
            };
            for (const auto &field : FIELDS)
            {
                JsonVariantConst value = doc[field[0]][field[1]];
                if (value.isNull())
                    continue;
                const bool port = std::strcmp(field[1], "port") == 0;
                if (port ? value.is<uint16_t>() : value.is<uint32_t>())
                    continue;
                return setError(error, std::string(field[0]) + "." + field[1] + " must be a whole number in range");
            }
            return true;
        }

        bool typeError(const JsonReader &reader, std::string *error)
        {
            const std::string &field = reader.field();
            switch (reader.expected())
            {
            case Expected::String:
                return setError(error, "Settings: " + field + " must be a string");
            case Expected::Boolean:
                return setError(error, "Settings: " + field + " must be true or false");
            case Expected::Number:
                return setError(error, "Settings: " + field + " must be a number");
            case Expected::WholeNumber:
                return setError(error, "Settings: " + field + " must be a whole number in range");
            case Expected::Object:
                break;
            }
            return setError(error, "Settings: " + field + " must be an object");
        }

        void applyRoot(const Section &doc, Config &cfg)
        {
            doc.take("deviceName", cfg.deviceName);
        }

        void applyWifi(const Section &wifi, WiFiConfig &w, bool keepSecrets)
        {
            wifi.take("ssid", w.ssid);
            assignSecret(wifi, "password", w.password, keepSecrets);
            wifi.take("hostname", w.hostname);
            wifi.take("mdns", w.mdns);
            wifi.take("ipv6", w.ipv6);
            wifi.take("autoReconnect", w.autoReconnect);
            wifi.take("reconnectDelayMs", w.reconnectDelayMs);
            wifi.take("maxReconnectDelayMs", w.maxReconnectDelayMs);
        }

        void applyMqtt(const Section &mqtt, MQTTConfig &m, bool keepSecrets)
        {
            mqtt.take("enabled", m.enabled);
            mqtt.take("broker", m.broker);
            mqtt.take("port", m.port);
            mqtt.take("username", m.username);
            assignSecret(mqtt, "password", m.password, keepSecrets);
            mqtt.take("topic", m.topic);
            const Section homeAssistant = mqtt.child("homeAssistant");
            homeAssistant.take("enabled", m.homeAssistant);
            homeAssistant.take("discoveryPrefix", m.discoveryPrefix);
            mqtt.take("publishIntervalMs", m.publishIntervalMs);
        }

        void applyAccess(const Section &doc, Config &cfg, bool keepSecrets)
        {
            const Section ota = doc.child("ota");
            ota.take("enabled", cfg.ota.enabled);
            assignSecret(ota, "password", cfg.ota.password, keepSecrets);

            const Section auth = doc.child("auth");
            auth.take("enabled", cfg.auth.enabled);
            auth.take("username", cfg.auth.username);
            assignSecret(auth, "password", cfg.auth.password, keepSecrets);
        }

        void applyTime(const Section &doc, Config &cfg)
        {
            const Section ntp = doc.child("ntp");
            ntp.take("enabled", cfg.ntp.enabled);
            ntp.take("server1", cfg.ntp.server1);
            ntp.take("server2", cfg.ntp.server2);
            ntp.take("timezone", cfg.ntp.timezone);
            ntp.take("syncIntervalMs", cfg.ntp.syncIntervalMs);
        }

        // Applies an "events" object ({"client_lost": {"level": 3, ...}, ...}).
        void applyEvents(const Section &events, AlertsConfig &a)
        {
            for (const auto &entry : eventSettings(a))
            {
                const Section event = events.child(entry.first);
                AlertsConfig::EventSetting &target = *entry.second;
                event.take("level", target.level);
                event.take("sound", target.sound);
                event.take("title", target.title);
                event.take("message", target.message);
            }
        }

        bool applySendMode(const Section &obj, AlertsConfig &a, std::string *error)
        {
            if (!obj.has("sendMode"))
                return true;
            const std::string mode = obj["sendMode"].is<const char *>() ? obj["sendMode"].as<const char *>() : "";
            if (mode == "any")
                a.sendMode = AlertsConfig::SendMode::Any;
            else if (mode == "whileConnected")
                a.sendMode = AlertsConfig::SendMode::WhileConnected;
            else
                return setError(error, "Alerts: when to send must be any or whileConnected");
            return true;
        }

        void applyClientSilence(const Section &obj, AlertsConfig &a)
        {
            obj.take("clientSilentSeconds", a.clientSilentSeconds);
        }

        void applyAlertChannels(const Section &alerts, AlertsConfig &a, bool keepSecrets)
        {
            const Section pushover = alerts.child("pushover");
            if (!pushover.isNull())
            {
                pushover.take("enabled", a.pushoverEnabled);
                assignSecret(pushover, "userKey", a.pushoverUserKey, keepSecrets);
                assignSecret(pushover, "appToken", a.pushoverAppToken, keepSecrets);
                // Pasted keys often carry a stray space or newline, which
                // Pushover rejects as "not a valid user".
                trimInPlace(a.pushoverUserKey);
                trimInPlace(a.pushoverAppToken);
                pushover.take("sound", a.pushoverSound);
            }

            const Section ntfy = alerts.child("ntfy");
            ntfy.take("enabled", a.ntfyEnabled);
            ntfy.take("server", a.ntfyServer);
            ntfy.take("topic", a.ntfyTopic);
            assignSecret(ntfy, "token", a.ntfyToken, keepSecrets);

            const Section webhook = alerts.child("webhook");
            webhook.take("enabled", a.webhookEnabled);
            webhook.take("url", a.webhookUrl);
            assignSecret(webhook, "authHeader", a.webhookAuthHeader, keepSecrets);
            webhook.take("insecureTls", a.webhookInsecureTls);

            alerts.child("mqtt").take("enabled", a.mqttEnabled);
        }

        bool applyAlerts(const Section &alerts, AlertsConfig &a, bool keepSecrets, std::string *error)
        {
            if (alerts.isNull())
                return true;
            alerts.take("enabled", a.enabled);
            const Section events = alerts.child("events");
            if (!events.isNull())
                applyEvents(events, a);
            if (!applySendMode(alerts, a, error))
                return false;
            applyClientSilence(alerts, a);
            alerts.take("cooldownSeconds", a.cooldownSeconds);
            applyAlertChannels(alerts, a, keepSecrets);
            return true;
        }

        // The imaging-app alert settings as stored under their own NVS key
        // (ConfigStore splices them in as "alertsClient").
        bool applyAlertsClient(const Section &client, AlertsConfig &a, std::string *error)
        {
            if (client.isNull())
                return true;
            const Section events = client.child("events");
            if (!events.isNull())
                applyEvents(events, a);
            if (!applySendMode(client, a, error))
                return false;
            applyClientSilence(client, a);
            return true;
        }

        bool isPushoverKey(const std::string &key)
        {
            if (key.size() != 30)
                return false;
            for (char c : key)
                if (!std::isalnum(static_cast<unsigned char>(c)))
                    return false;
            return true;
        }

        // Rules only enforced for changes coming from the UI/API, so a value
        // stored by older firmware never stops the config from loading: host
        // and URL forms (IPv6 literals, spec 015) and Pushover key format.
        bool validateIncoming(const Config &cfg, std::string *error)
        {
            if (!validateAddresses(cfg, error))
                return false;
            if (!cfg.alerts.pushoverEnabled)
                return true;
            if (!isPushoverKey(cfg.alerts.pushoverUserKey))
                return setError(error, "Pushover user key must be 30 letters and digits");
            if (!isPushoverKey(cfg.alerts.pushoverAppToken))
                return setError(error, "Pushover app token must be 30 letters and digits");
            return true;
        }
        // Every section, in order. False (with the reason) on a value that
        // can never be stored.
        bool applySections(const Section &root, Config &cfg, bool keep, std::string *error)
        {
            applyRoot(root, cfg);
            applyWifi(root.child("wifi"), cfg.wifi, keep);
            applyMqtt(root.child("mqtt"), cfg.mqtt, keep);
            applyAccess(root, cfg, keep);
            applyTime(root, cfg);
            root.child("alpaca").take("enabled", cfg.alpaca.enabled);
            return applyAlerts(root.child("alerts"), cfg.alerts, keep, error) &&
                   applyAlertsClient(root.child("alertsClient"), cfg.alerts, error);
        }
    } // namespace

    bool Config::applyJson(const std::string &json, Config &cfg, bool preserveSecretPlaceholders, std::string *errorOut)
    {
        // Parsing copies every string, and custom alert texts can make the JSON
        // much bigger than the defaults.
        DynamicJsonDocument doc(json.size() + 4096 > 6144 ? json.size() + 4096 : 6144);
        const DeserializationError error = deserializeJson(doc, json);
        if (error)
            return setError(errorOut, std::string("Settings JSON couldn't be read: ") + error.c_str());
        if (!checkWholeNumbers(doc, errorOut))
            return false;

        const bool keep = preserveSecretPlaceholders;
        // Changes from the UI/API are strict about types; stored settings
        // keep going (see JsonReader).
        JsonReader reader(preserveSecretPlaceholders);
        if (reader.strict() && !doc.is<JsonObject>())
            return setError(errorOut, "Settings must be a JSON object");
        const Section root(reader, doc.as<JsonVariant>(), "");
        const std::string previousSsid = cfg.wifi.ssid;
        if (!applySections(root, cfg, keep, errorOut))
            return false;
        if (reader.failed())
            return typeError(reader, errorOut);
        // Saving settings never takes the device off its network: changing
        // networks is WiFi setup's job (POST /api/wifi/connect).
        if (reader.strict() && isBlank(cfg.wifi.ssid) && !isBlank(previousSsid))
            return setError(errorOut, "WiFi network can't be blank; use WiFi setup to change networks");

        if (!cfg.validate(errorOut))
            return false;
        return !preserveSecretPlaceholders || validateIncoming(cfg, errorOut);
    }

    std::optional<std::string> Config::mqttTestPassword(const MQTTConfig &saved, const MqttTestTarget &target)
    {
        if (!isPlaceholderSecret(target.password.c_str()) || saved.password.empty())
            return target.password;
        if (target.broker == saved.broker && target.port == saved.port && target.username == saved.username)
            return saved.password;
        if (target.password.empty())
            return target.password; // testing another broker without a password
        return std::nullopt;
    }
} // namespace SQM
