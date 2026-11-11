#include "SettingsDeps.h"

#include <cstring>
#include <initializer_list>

namespace SQM
{
    namespace Deps
    {
        namespace
        {
            // Same codes, texts and fix targets as catalogue.json (a native
            // test compares them).
            const std::vector<Reason> REASONS = {
                {"alerts-off", "Alerts are off", "alerts#alerts"},
                {"mqtt-off", "MQTT is off", "network#mqtt"},
                {"mqtt-disconnected", "Broker not connected", "network#mqtt"},
                {"wifi-disconnected", "Not connected to WiFi", "network#wifi"},
                {"alpaca-off", "Alpaca is off", "network#alpaca"},
                {"no-alpaca-device", "No Alpaca device", "network#alpaca"},
                {"ota-no-password", "Set an upload password", "device#security"},
                {"daylight-off", "Daylight is off", "roof#daylight"},
                {"location-unknown", "Location not set", "roof#daylight"},
                {"clock-not-set", "Clock not set", "time#ntp"},
                {"remote-off", "Alpaca and MQTT roof commands are off", "network#alpaca"},
                {"diag-off", "Advanced diagnostics is off", "device#diagnostics"},
            };

            const Reason *reason(const char *code)
            {
                for (const Reason &r : REASONS)
                    if (std::strcmp(r.code, code) == 0)
                        return &r;
                return nullptr;
            }

            // One link of a dependency chain: met, or the catalogue ID and
            // reason that make the setting inactive.
            struct Link
            {
                const char *id;
                const char *reason;
                bool met;
            };

            // How a reported setting is shown: safety rules declare their unmet
            // behaviour; harmless defaults are shown muted.
            struct Shown
            {
                Unmet unmet = Unmet::None;
                bool neutral = false;
            };

            // What every section needs, worked out once.
            struct Context
            {
                const Config &cfg;
                const Facts &f;
                std::vector<Entry> &out;

                // `on`: the setting's own switch. The first unmet link decides.
                void add(const char *setting, const char *id, bool on, std::initializer_list<Link> chain, Shown shown = {}) const
                {
                    Entry e;
                    e.setting = setting;
                    e.id = id;
                    e.unmet = shown.unmet;
                    e.neutral = shown.neutral;
                    if (on)
                    {
                        e.state = State::Active;
                        for (const Link &link : chain)
                        {
                            if (link.met)
                                continue;
                            e.state = State::Inactive;
                            e.id = link.id;
                            e.reason = reason(link.reason);
                            break;
                        }
                    }
                    out.push_back(e);
                }
            };

            // Alert channels are push only: they need the master switch. Links
            // in catalogue order, so the channel's own dependency comes before
            // "alerts are off" - a test send ignores the latter.
            void addAlertChannels(const Context &c)
            {
                const AlertsConfig &a = c.cfg.alerts;
                const Link alertsOn{"D-04", "alerts-off", a.enabled};
                const Link wifi{"D-03", "wifi-disconnected", c.f.wifiConnected};
                c.add("alerts.pushover.enabled", "D-03", a.pushoverEnabled, {wifi, alertsOn});
                c.add("alerts.ntfy.enabled", "D-03", a.ntfyEnabled, {wifi, alertsOn});
                c.add("alerts.webhook.enabled", "D-03", a.webhookEnabled, {wifi, alertsOn});
                const Link mqttOn{"D-01", "mqtt-off", c.cfg.mqtt.enabled};
                const Link broker{"D-02", "mqtt-disconnected", c.f.mqttConnected};
                c.add("alerts.mqtt.enabled", "D-01", a.mqttEnabled, {mqttOn, broker, alertsOn});
            }

            // Imaging apps connect over Alpaca (SQMeter spec 021), to a device
            // it offers.
            void addAlertEvents(const Context &c)
            {
                const AlertsConfig &a = c.cfg.alerts;
                const Link alertsOn{"D-04", "alerts-off", a.enabled};
                const Link alpacaOn{"D-37", "alpaca-off", c.cfg.alpaca.enabled};
                const Link device{"D-37", "no-alpaca-device", c.f.alpacaDevices > 0};
                c.add("alerts.events.client_lost.level", "D-37", a.clientLost.level != 0, {alertsOn, alpacaOn, device});
                c.add("alerts.events.client_back.level", "D-37", a.clientBack.level != 0, {alertsOn, alpacaOn, device});
                c.add("alerts.events.client_disconnected.level", "D-37", a.clientDisconnected.level != 0, {alertsOn, alpacaOn, device});
                // The roof's non-critical events need alerts on; the critical
                // two (mount not parked, roof didn't close) are sent whatever
                // the master switch says, so they depend on nothing (spec 007).
                c.add("alerts.events.move_fault.level", "D-04", a.moveFault.level != 0, {alertsOn});
                c.add("alerts.events.supply_low.level", "D-04", a.supplyLow.level != 0, {alertsOn});
                c.add("alerts.events.unsafe_closing.level", "D-04", a.unsafeClosing.level != 0, {alertsOn});
                c.add("alerts.events.roof_closed.level", "D-04", a.roofClosed.level != 0, {alertsOn});
                c.add("alerts.events.safety_lost.level", "D-04", a.safetyLost.level != 0, {alertsOn});
                // Spec 013: the daylight events need alerts on and daylight on.
                const Link daylightOn{"D-44", "daylight-off", c.cfg.daylight.mode != DaylightConfig::Mode::Off};
                c.add("alerts.events.daylight_open.level", "D-04", a.daylightOpen.level != 0, {alertsOn, daylightOn});
                c.add("alerts.events.daylight_closed.level", "D-04", a.daylightClosed.level != 0, {alertsOn, daylightOn});
                c.add("alerts.events.daylight_unknown.level", "D-04", a.daylightUnknown.level != 0, {alertsOn, daylightOn});
            }

            // Spec 013: the daylight check needs the site and the clock; its
            // threshold and the site only matter while it is on.
            void addDaylight(const Context &c)
            {
                const Config &cfg = c.cfg;
                const bool on = cfg.daylight.mode != DaylightConfig::Mode::Off;
                c.add(
                    "daylight.mode", "D-42", on, {{"D-42", "location-unknown", cfg.location.set}, {"D-43", "clock-not-set", c.f.clockSet}});
                const Link daylightOn{"D-44", "daylight-off", on};
                c.add("daylight.sunAltitudeDeg", "D-44", true, {daylightOn});
                c.add("daylight.bandDeg", "D-44", true, {daylightOn});
                // Spec 014: a remote stop comes from an imaging app (Alpaca) or MQTT roof commands.
                const bool remote = cfg.alpaca.enabled || (cfg.mqtt.enabled && cfg.mqtt.roofCommands);
                c.add("autoClose.remoteStop", "D-45", cfg.autoClose.remoteStop, {{"D-45", "remote-off", remote}});
                c.add("location.set", "D-44", cfg.location.set, {daylightOn});
                c.add("location.latitude", "D-44", cfg.location.set, {daylightOn});
                c.add("location.longitude", "D-44", cfg.location.set, {daylightOn});
            }

            void addAlertOptions(const Context &c)
            {
                const AlertsConfig &a = c.cfg.alerts;
                c.add(
                    "alerts.sendMode",
                    "D-12",
                    a.sendMode == AlertsConfig::SendMode::WhileConnected,
                    {{"D-04", "alerts-off", a.enabled}, {"D-12", "alpaca-off", c.cfg.alpaca.enabled}});
            }

            void addMqtt(const Context &c)
            {
                const MQTTConfig &mqtt = c.cfg.mqtt;
                const Link mqttOn{"D-13", "mqtt-off", mqtt.enabled};
                c.add("mqtt.homeAssistant.enabled", "D-13", mqtt.homeAssistant, {mqttOn});
                const Link alerting{"D-13", "alerts-off", c.cfg.alerts.enabled};
                c.add("mqtt.homeAssistant.alertsSwitch", "D-13", mqtt.homeAssistant, {mqttOn, alerting});
            }

            // The SQMeter MQTT safety source is read from RoofRoof's own broker (spec 004).
            void addSafety(const Context &c)
            {
                const Link mqttOn{"D-40", "mqtt-off", c.cfg.mqtt.enabled};
                const Link broker{"D-41", "mqtt-disconnected", c.f.mqttConnected};
                c.add("safety.mqtt.enabled", "D-40", c.cfg.safety.mqttEnabled, {mqttOn, broker});
            }

            void addDevice(const Context &c)
            {
                const Config &cfg = c.cfg;
                c.add("ntp.enabled", "D-28", cfg.ntp.enabled, {{"D-28", "wifi-disconnected", c.f.wifiConnected}});
                c.add("ota.enabled", "D-32", cfg.ota.enabled, {{"D-32", "ota-no-password", !cfg.ota.password.empty()}});
                c.add("wifi.mdns", "D-36", cfg.wifi.mdns, {{"D-36", "wifi-disconnected", c.f.wifiConnected}});
                c.add("wifi.ipv6", "D-36", cfg.wifi.ipv6, {{"D-36", "wifi-disconnected", c.f.wifiConnected}});
            }

            // Spec 017: the kept summaries and the MQTT topics belong to
            // advanced diagnostics; the topics also need MQTT.
            void addDiag(const Context &c)
            {
                const DiagConfig &d = c.cfg.diag;
                const Link diagOn{"D-46", "diag-off", d.enabled};
                c.add("diag.keepSummaries", "D-46", d.keepSummaries, {diagOn});
                c.add("diag.mqtt", "D-46", d.mqtt, {diagOn, {"D-46", "mqtt-off", c.cfg.mqtt.enabled}});
            }
        } // namespace

        std::vector<Entry> evaluate(const Config &cfg, const Facts &f)
        {
            std::vector<Entry> out;
            out.reserve(16);
            const Context c{cfg, f, out};
            addAlertChannels(c);
            addAlertEvents(c);
            addAlertOptions(c);
            addMqtt(c);
            addSafety(c);
            addDaylight(c);
            addDevice(c);
            addDiag(c);
            return out;
        }

        const Entry *find(const std::vector<Entry> &entries, const char *setting)
        {
            for (const Entry &e : entries)
                if (std::strcmp(e.setting, setting) == 0)
                    return &e;
            return nullptr;
        }

        bool isActive(const std::vector<Entry> &entries, const char *setting)
        {
            const Entry *e = find(entries, setting);
            return e != nullptr && e->state == State::Active;
        }

        const Reason *reasonFor(const std::vector<Entry> &entries, const char *setting)
        {
            const Entry *e = find(entries, setting);
            return e != nullptr && e->state == State::Inactive ? e->reason : nullptr;
        }

        const std::vector<Reason> &reasons()
        {
            return REASONS;
        }

        const char *stateName(State state)
        {
            switch (state)
            {
            case State::Active:
                return "active";
            case State::Inactive:
                return "inactive";
            default:
                return "off";
            }
        }

        const char *unmetName(Unmet unmet)
        {
            switch (unmet)
            {
            case Unmet::Inactive:
                return "inactive";
            case Unmet::FailSafe:
                return "fail-safe";
            default:
                return nullptr;
            }
        }

        void writeFacts(JsonObject target, const Facts &f)
        {
            target["wifiConnected"] = f.wifiConnected;
            target["mqttConnected"] = f.mqttConnected;
            target["clockSet"] = f.clockSet;
            target["alpacaDevices"] = f.alpacaDevices;
        }

        size_t reportCapacity(const std::vector<Entry> &entries)
        {
            return JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) + JSON_ARRAY_SIZE(entries.size()) + entries.size() * JSON_OBJECT_SIZE(8);
        }

        void writeReport(JsonObject root, const std::vector<Entry> &entries, const Facts &facts)
        {
            writeFacts(root.createNestedObject("facts"), facts);
            JsonArray list = root.createNestedArray("settings");
            for (const Entry &e : entries)
            {
                JsonObject item = list.createNestedObject();
                item["id"] = e.id;
                item["setting"] = e.setting;
                item["state"] = stateName(e.state);
                if (e.state == State::Inactive && e.reason != nullptr)
                {
                    item["reason"] = e.reason->code;
                    item["text"] = e.reason->text;
                    item["fix"] = e.reason->fix;
                }
                if (e.unmet != Unmet::None)
                    item["unmet"] = unmetName(e.unmet);
                if (e.neutral)
                    item["neutral"] = true;
            }
        }
    } // namespace Deps
} // namespace SQM
