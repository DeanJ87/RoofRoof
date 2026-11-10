#include "DeviceCore.h"

#include <cctype>
#include <cstdio>

// The alert side of the device core: the rules and wording from settings,
// the imaging-app (Alpaca client) watch, the send schedule documents and test
// notifications.

namespace SQM
{
    namespace Core
    {
        Alerts::AlertRules alertRules(const Config &cfg)
        {
            Alerts::AlertRules rules;
            const AlertsConfig &a = cfg.alerts;
            rules.cooldownSeconds = a.cooldownSeconds;
            rules.onClientLost = a.clientLost.level != 0;
            rules.onClientBack = a.clientBack.level != 0;
            rules.onClientDisconnected = a.clientDisconnected.level != 0;
            return rules;
        }

        const AlertsConfig::EventSetting *eventSettingFor(const AlertsConfig &a, Alerts::AlertType type)
        {
            using Alerts::AlertType;
            using Setting = AlertsConfig::EventSetting AlertsConfig::*;
            static constexpr std::pair<AlertType, Setting> SETTINGS[] = {
                {AlertType::ClientLost, &AlertsConfig::clientLost},
                {AlertType::ClientBack, &AlertsConfig::clientBack},
                {AlertType::ClientDisconnected, &AlertsConfig::clientDisconnected},
            };
            for (const auto &entry : SETTINGS)
                if (entry.first == type)
                    return &(a.*entry.second);
            return nullptr;
        }

        std::vector<std::pair<std::string, std::string>> alertVars(
            const AlertSources &sources, const Alerts::Alert &alert, const LocalClock &clock)
        {
            // The device's values first; the event's own (device, silent_for,
            // ...) come after and win on a name clash.
            std::vector<std::pair<std::string, std::string>> vars = {
                {"device", sources.cfg.deviceName},
                {"event", Alerts::alertTypeName(alert.type)},
                {"level", Alerts::alertLevelName(alert.level)},
                {"time", clock.time},
                {"date", clock.date},
            };
            vars.insert(vars.end(), alert.vars.begin(), alert.vars.end());
            return vars;
        }

        void applyAlertTemplate(
            Alerts::Alert &alert, const AlertsConfig::EventSetting &setting, const std::vector<std::pair<std::string, std::string>> &vars)
        {
            if (!setting.title.empty())
                alert.title = Alerts::renderTemplate(setting.title, vars);
            if (!setting.message.empty())
                alert.message = Alerts::renderTemplate(setting.message, vars);
            // Not needed past this point, and the recent-alerts list keeps alerts.
            alert.vars.clear();
            alert.vars.shrink_to_fit();
        }

        void clientSilenceMs(const Config &cfg, uint32_t (&out)[Alpaca::MAX_DEVICES])
        {
            for (uint32_t &silence : out)
                silence = cfg.alerts.clientSilentSeconds * 1000;
        }

        namespace
        {
            // The local clock time `ageSeconds` ago, from the current "HH:MM";
            // "N min ago" when there's no clock.
            std::string lastCheckedText(uint32_t ageSeconds, const std::string &localTime)
            {
                const bool clock = localTime.size() == 5 && localTime[2] == ':' && std::isdigit(static_cast<unsigned char>(localTime[0])) &&
                                   std::isdigit(static_cast<unsigned char>(localTime[1])) &&
                                   std::isdigit(static_cast<unsigned char>(localTime[3])) &&
                                   std::isdigit(static_cast<unsigned char>(localTime[4]));
                if (!clock)
                    return formatDuration(ageSeconds) + " ago";
                const int now = std::stoi(localTime.substr(0, 2)) * 60 + std::stoi(localTime.substr(3, 2));
                const int then = ((now - static_cast<int>(ageSeconds / 60)) % 1440 + 1440) % 1440;
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "%02d:%02d", then / 60, then % 60);
                return buffer;
            }
        } // namespace

        void addClientInputs(
            Alerts::AlertInputs &inputs,
            const Alpaca::ClientWatch &watch,
            const std::vector<Alpaca::DeviceInfo> &devices,
            const Config &cfg,
            uint32_t nowMs,
            const std::string &localTime)
        {
            uint32_t silence[Alpaca::MAX_DEVICES];
            clientSilenceMs(cfg, silence);
            inputs.clientCount = devices.size() < Alerts::MAX_CLIENT_DEVICES ? devices.size() : Alerts::MAX_CLIENT_DEVICES;
            for (size_t i = 0; i < inputs.clientCount; ++i)
            {
                const Alpaca::ClientState &state = watch.state(i);
                Alerts::ClientInputs &client = inputs.clients[i];
                client.key = devices[i].typeKey.c_str();
                client.device = devices[i].name.c_str();
                client.watching = state.watching;
                client.silent = state.silent;
                client.disconnectedNow = state.disconnectedNow;
                client.silentFor = formatDuration(silence[i] / 1000);
                client.lastChecked = state.everRequested ? lastCheckedText((nowMs - state.lastRequestMs) / 1000, localTime) : "never";
                client.clientId = state.hasClientId ? std::to_string(state.clientId) : "";
            }
        }

        void writeClientWatch(
            JsonObject alpaca, const Alpaca::ClientWatch &watch, const std::vector<Alpaca::DeviceInfo> &devices, const Config &cfg, uint32_t nowMs)
        {
            alpaca["enabled"] = cfg.alpaca.enabled;
            JsonObject clients = alpaca.createNestedObject("clients");
            for (size_t i = 0; i < devices.size() && i < Alpaca::MAX_DEVICES; ++i)
            {
                const Alpaca::ClientState &state = watch.state(i);
                JsonObject client = clients.createNestedObject(devices[i].typeKey);
                client["connected"] = state.connected;
                client["watching"] = state.watching;
                client["silent"] = state.silent;
                if (state.everRequested)
                    client["lastCheckedAgeMs"] = nowMs - state.lastRequestMs;
                else
                    client["lastCheckedAgeMs"] = nullptr;
                if (state.hasClientId)
                    client["clientId"] = state.clientId;
                else
                    client["clientId"] = nullptr;
            }
        }

        Alerts::SendMode sendMode(const Config &cfg)
        {
            return cfg.alerts.sendMode == AlertsConfig::SendMode::WhileConnected ? Alerts::SendMode::WhileConnected : Alerts::SendMode::Any;
        }

        Alerts::SendMode effectiveSendMode(const Config &cfg)
        {
            // dep: D-12 - imaging apps connect over Alpaca: with it off, "only while
            // an imaging app is connected" isn't in effect and alerts go out any time.
            return cfg.alpaca.enabled ? sendMode(cfg) : Alerts::SendMode::Any;
        }

        void writeAlertSchedule(JsonObject target, const Alerts::ScheduleState &state, const Config &cfg, uint32_t nowMs)
        {
            target["armed"] = state.sending;
            target["mode"] = Alerts::sendModeName(sendMode(cfg));
            target["reason"] = Alerts::scheduleReasonName(state.reason);
            const std::string since = isoUtc(state.sinceEpoch);
            if (since.empty())
                target["since"] = nullptr;
            else
                target["since"] = since;
            if (state.sinceKnown)
                target["sinceAgeMs"] = nowMs - state.sinceMs;
            else
                target["sinceAgeMs"] = nullptr;
        }

        AlertStep runAlerts(
            Alerts::AlertEngine &engine,
            const Alerts::AlertInputs &inputs,
            const Alerts::AlertRules &rules,
            const AlertSources &sources,
            const LocalClock &clock)
        {
            const Config &cfg = sources.cfg;
            AlertStep step;
            for (Alerts::Alert alert : engine.update(inputs, rules))
            {
                const AlertsConfig::EventSetting *setting = eventSettingFor(cfg.alerts, alert.type);
                if (setting == nullptr || setting->level == 0)
                    continue;
                alert.level = static_cast<Alerts::AlertLevel>(setting->level);
                alert.sound = setting->sound;
                applyAlertTemplate(alert, *setting, alertVars(sources, alert, clock));
                step.outgoing.push_back(std::move(alert));
            }
            return step;
        }

        namespace
        {
            constexpr SampleAlert SAMPLE_ALERTS[] = {
                {"client_lost", Alerts::AlertType::ClientLost, "Imaging app stopped checking", "The imaging app stops checking"},
                {"client_back", Alerts::AlertType::ClientBack, "Imaging app is back", "The imaging app is back"},
                {"client_disconnected", Alerts::AlertType::ClientDisconnected, "Imaging app disconnected", "The imaging app disconnects"},
            };
        } // namespace

        const SampleAlert *sampleAlert(const std::string &key)
        {
            for (const SampleAlert &sample : SAMPLE_ALERTS)
                if (key == sample.key)
                    return &sample;
            return nullptr;
        }

        Alerts::Alert buildTestAlert(
            const SampleAlert *sample, const TestWording &wording, const AlertSources &sources, const LocalClock &clock)
        {
            const Config &cfg = sources.cfg;
            const uint8_t level = wording.level;
            Alerts::Alert test;
            if (sample == nullptr)
            {
                test.type = Alerts::AlertType::Test;
                test.level = Alerts::AlertLevel::Normal;
                test.title = "Test notification";
                test.message = "Alerts from this RoofRoof are working.";
                return test;
            }
            test.type = sample->type;
            test.level = static_cast<Alerts::AlertLevel>(level);
            test.sound = wording.sound;
            test.title = sample->title;
            test.message = std::string("This is how a \"") + sample->label + "\" alert arrives.";

            // Values only a real event has are examples.
            test.vars = {
                {"device", "RoofRoof Dome (example)"},
                {"silent_for", formatDuration(cfg.alerts.clientSilentSeconds)},
                {"last_checked", "2 min ago (example)"},
                {"client_id", "1234 (example)"}};
            AlertsConfig::EventSetting custom{level, wording.sound, wording.title, wording.message};
            applyAlertTemplate(test, custom, alertVars(sources, test, clock));
            test.title = "Test: " + test.title;
            return test;
        }
    } // namespace Core
} // namespace SQM
