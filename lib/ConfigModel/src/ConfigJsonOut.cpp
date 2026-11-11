#include "Config.h"
#include "ConfigInternal.h"
#include <ArduinoJson.h>

// Settings as JSON, out: toJson() and alertsToJson(). One function per
// section, in the order the sections appear in the document.

namespace SQM
{
    using namespace ConfigDetail;

    namespace
    {
        const char *secretOut(const std::string &value, bool redact)
        {
            return redact && !value.empty() ? SECRET_MASK : value.c_str();
        }

        void appendEvents(JsonObject alerts, const AlertsConfig &a, size_t first, size_t last)
        {
            JsonObject events = alerts.createNestedObject("events");
            const auto settings = eventSettings(a);
            for (size_t i = first; i < last; ++i)
            {
                JsonObject event = events.createNestedObject(settings[i].first);
                event["level"] = settings[i].second->level;
                event["sound"] = settings[i].second->sound.c_str();
                event["title"] = settings[i].second->title.c_str();
                event["message"] = settings[i].second->message.c_str();
            }
        }

        void appendClientSilence(JsonObject alerts, const AlertsConfig &a)
        {
            alerts["sendMode"] = sendModeName(a.sendMode);
            alerts["clientSilentSeconds"] = a.clientSilentSeconds;
        }

        void appendAlertChannels(JsonObject alerts, const AlertsConfig &a, bool redact)
        {
            JsonObject pushover = alerts.createNestedObject("pushover");
            pushover["enabled"] = a.pushoverEnabled;
            pushover["userKey"] = secretOut(a.pushoverUserKey, redact);
            pushover["appToken"] = secretOut(a.pushoverAppToken, redact);
            pushover["sound"] = a.pushoverSound.c_str();

            JsonObject ntfy = alerts.createNestedObject("ntfy");
            ntfy["enabled"] = a.ntfyEnabled;
            ntfy["server"] = a.ntfyServer.c_str();
            ntfy["topic"] = a.ntfyTopic.c_str();
            ntfy["token"] = secretOut(a.ntfyToken, redact);

            JsonObject webhook = alerts.createNestedObject("webhook");
            webhook["enabled"] = a.webhookEnabled;
            webhook["url"] = a.webhookUrl.c_str();
            webhook["authHeader"] = secretOut(a.webhookAuthHeader, redact);
            webhook["insecureTls"] = a.webhookInsecureTls;

            JsonObject mqtt = alerts.createNestedObject("mqtt");
            mqtt["enabled"] = a.mqttEnabled;
        }

        void appendAlerts(JsonObject alerts, const AlertsConfig &a, bool redactSecrets, Config::AlertsPart part)
        {
            if (part == Config::AlertsPart::Client)
            {
                appendEvents(alerts, a, FIRST_CLIENT_EVENT, EVENT_COUNT);
                appendClientSilence(alerts, a);
                return;
            }

            alerts["enabled"] = a.enabled;
            appendEvents(alerts, a, 0, part == Config::AlertsPart::All ? EVENT_COUNT : FIRST_CLIENT_EVENT);
            if (part == Config::AlertsPart::All)
                appendClientSilence(alerts, a);
            alerts["cooldownSeconds"] = a.cooldownSeconds;
            appendAlertChannels(alerts, a, redactSecrets);
        }

        void appendNetwork(JsonDocument &doc, const Config &c, bool redact)
        {
            JsonObject wifi = doc.createNestedObject("wifi");
            wifi["ssid"] = c.wifi.ssid;
            wifi["password"] = secretOut(c.wifi.password, redact);
            wifi["hostname"] = c.wifi.hostname;
            wifi["mdns"] = c.wifi.mdns;
            wifi["ipv6"] = c.wifi.ipv6;
            wifi["autoReconnect"] = c.wifi.autoReconnect;
            wifi["reconnectDelayMs"] = c.wifi.reconnectDelayMs;
            wifi["maxReconnectDelayMs"] = c.wifi.maxReconnectDelayMs;

            JsonObject mqtt = doc.createNestedObject("mqtt");
            mqtt["enabled"] = c.mqtt.enabled;
            mqtt["broker"] = c.mqtt.broker;
            mqtt["port"] = c.mqtt.port;
            mqtt["username"] = c.mqtt.username;
            mqtt["password"] = secretOut(c.mqtt.password, redact);
            mqtt["topic"] = c.mqtt.topic;
            mqtt["publishIntervalMs"] = c.mqtt.publishIntervalMs;
            JsonObject homeAssistant = mqtt.createNestedObject("homeAssistant");
            homeAssistant["enabled"] = c.mqtt.homeAssistant;
            homeAssistant["discoveryPrefix"] = c.mqtt.discoveryPrefix;

            JsonObject ota = doc.createNestedObject("ota");
            ota["enabled"] = c.ota.enabled;
            ota["password"] = secretOut(c.ota.password, redact);

            JsonObject auth = doc.createNestedObject("auth");
            auth["enabled"] = c.auth.enabled;
            auth["username"] = c.auth.username;
            auth["password"] = secretOut(c.auth.password, redact);
        }

        void appendTime(JsonDocument &doc, const Config &c)
        {
            JsonObject ntp = doc.createNestedObject("ntp");
            ntp["enabled"] = c.ntp.enabled;
            ntp["server1"] = c.ntp.server1;
            ntp["server2"] = c.ntp.server2;
            ntp["timezone"] = c.ntp.timezone;
            ntp["syncIntervalMs"] = c.ntp.syncIntervalMs;
        }

        void appendSafety(JsonDocument &doc, const Config &c)
        {
            JsonObject safety = doc.createNestedObject("safety");
            JsonObject http = safety.createNestedObject("http");
            http["enabled"] = c.safety.httpEnabled;
            http["url"] = c.safety.httpUrl;
            JsonObject mqtt = safety.createNestedObject("mqtt");
            mqtt["enabled"] = c.safety.mqttEnabled;
            mqtt["topic"] = c.safety.mqttTopic;
            safety["staleSeconds"] = c.safety.staleSeconds;
            safety["rainContact"] = c.safety.rainContact;

            JsonObject park = doc.createNestedObject("park");
            JsonObject nina = park.createNestedObject("nina");
            nina["url"] = c.park.ninaUrl;
            nina["api"] = "v2";
            park["switches"] = switchesName(c.park.switches);
            park["timeoutSeconds"] = c.park.timeoutSeconds;
        }

        void appendRoof(JsonDocument &doc, const RoofConfig &r)
        {
            JsonObject roof = doc.createNestedObject("roof");
            roof["travelMm"] = r.travelMm;
            roof["speedMmPerSecond"] = r.speedMmPerSecond;
            roof["timeoutMarginSeconds"] = r.timeoutMarginSeconds;
            roof["leaveMagnetSeconds"] = r.leaveMagnetSeconds;
            roof["rampUpMs"] = r.rampUpMs;
            roof["rampDownMs"] = r.rampDownMs;
            roof["topDutyPercent"] = r.topDutyPercent;
            roof["slowDutyPercent"] = r.slowDutyPercent;
            roof["openDirection"] = r.openDirection == RoofConfig::Direction::Reverse ? "reverse" : "forward";
            roof["hallHighEnd"] = r.hallHighEnd == RoofConfig::HallEnd::Closed ? "closed" : "open";
            roof["hallHighCounts"] = r.hallHighCounts;
            roof["hallLowCounts"] = r.hallLowCounts;
            roof["currentSenseEnabled"] = r.currentSenseEnabled;
            roof["maxCurrentAmps"] = r.maxCurrentAmps;
            roof["currentAmpsPerVolt"] = r.currentAmpsPerVolt;
            roof["minRunningAmps"] = r.minRunningAmps;
            roof["minSupplyVolts"] = r.minSupplyVolts;
        }
    } // namespace

    std::string Config::alertsToJson(bool redactSecrets, AlertsPart part) const
    {
        DynamicJsonDocument doc(3072); // strings are referenced, not copied
        appendAlerts(doc.to<JsonObject>(), alerts, redactSecrets, part);
        std::string json;
        serializeJson(doc, json);
        return json;
    }

    std::string Config::toJson(bool redactSecrets, bool includeAlerts) const
    {
        DynamicJsonDocument doc(4608);
        doc["deviceName"] = deviceName;
        appendNetwork(doc, *this, redactSecrets);
        appendTime(doc, *this);
        doc.createNestedObject("alpaca")["enabled"] = alpaca.enabled;
        appendSafety(doc, *this);
        appendRoof(doc, roof);
        if (includeAlerts)
            appendAlerts(doc.createNestedObject("alerts"), alerts, redactSecrets, AlertsPart::All);

        std::string output;
        serializeJson(doc, output);
        return output;
    }

} // namespace SQM
