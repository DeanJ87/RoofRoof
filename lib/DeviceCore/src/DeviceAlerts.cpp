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
            rules.onMoveFault = a.moveFault.level != 0;
            rules.onSupplyLow = a.supplyLow.level != 0;
            rules.onUnsafeClosing = a.unsafeClosing.level != 0;
            rules.onRoofClosed = a.roofClosed.level != 0;
            rules.onSafetyLost = a.safetyLost.level != 0;
            rules.onDaylightOpen = a.daylightOpen.level != 0;
            rules.onDaylightClosed = a.daylightClosed.level != 0;
            rules.onDaylightUnknown = a.daylightUnknown.level != 0;
            return rules;
        }

        const AlertsConfig::EventSetting *eventSettingFor(const AlertsConfig &a, Alerts::AlertType type)
        {
            using Alerts::AlertType;
            using Setting = AlertsConfig::EventSetting AlertsConfig::*;
            static constexpr std::pair<AlertType, Setting> SETTINGS[] = {
                {AlertType::MountNotParked, &AlertsConfig::mountNotParked},
                {AlertType::RoofNotClosed, &AlertsConfig::roofNotClosed},
                {AlertType::MoveFault, &AlertsConfig::moveFault},
                {AlertType::SupplyLow, &AlertsConfig::supplyLow},
                {AlertType::UnsafeClosing, &AlertsConfig::unsafeClosing},
                {AlertType::RoofClosed, &AlertsConfig::roofClosed},
                {AlertType::SafetyLost, &AlertsConfig::safetyLost},
                {AlertType::ClientLost, &AlertsConfig::clientLost},
                {AlertType::ClientBack, &AlertsConfig::clientBack},
                {AlertType::ClientDisconnected, &AlertsConfig::clientDisconnected},
                {AlertType::DaylightOpen, &AlertsConfig::daylightOpen},
                {AlertType::DaylightClosed, &AlertsConfig::daylightClosed},
                {AlertType::DaylightUnknown, &AlertsConfig::daylightUnknown},
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

        namespace
        {
            std::string varOf(const std::vector<std::pair<std::string, std::string>> &vars, const char *name)
            {
                std::string value;
                for (const auto &var : vars)
                    if (var.first == name)
                        value = var.second;
                return value;
            }

            // A critical event's level: the setting's, never below urgent
            // (constitution II and III; spec 007 FR-003).
            Alerts::AlertLevel criticalLevel(uint8_t level)
            {
                if (level < AlertsConfig::MIN_CRITICAL_LEVEL)
                    level = AlertsConfig::MIN_CRITICAL_LEVEL;
                return static_cast<Alerts::AlertLevel>(level > 4 ? 4 : level);
            }
        } // namespace

        namespace
        {
            // Whether rendering `text` fills in {reason} or {details}: rendered
            // with markers in their place, as renderTemplate() reads it (so
            // "{{reason}" or a typo doesn't count).
            bool usesReason(const std::string &text, std::vector<std::pair<std::string, std::string>> vars)
            {
                static const std::string MARK = "\x01\x02";
                for (auto &var : vars)
                    if (var.first == "reason" || var.first == "details")
                        var.second = MARK;
                vars.push_back({"reason", MARK});
                vars.push_back({"details", MARK});
                return Alerts::renderTemplate(text, vars).find(MARK) != std::string::npos;
            }
        } // namespace

        void applyCriticalTemplate(
            Alerts::Alert &alert, const AlertsConfig::EventSetting &setting, const std::vector<std::pair<std::string, std::string>> &vars)
        {
            const std::string builtIn = alert.message;
            const std::string builtInTitle = alert.title;
            if (!setting.title.empty())
            {
                alert.title = Alerts::renderTemplate(setting.title, vars);
                if (varOf(vars, "reminder") == "yes" && alert.title.rfind("Still: ", 0) != 0)
                    alert.title = "Still: " + alert.title;
                // Pushover refuses titles over 250 characters: too long falls
                // back to the built-in title (which carries "Still: " itself).
                if (alert.title.size() > MAX_CRITICAL_TITLE_BYTES)
                    alert.title = builtInTitle;
            }
            if (!setting.message.empty())
            {
                alert.message = Alerts::renderTemplate(setting.message, vars);
                // The reason is never lost (constitution III; spec 007 FR-005).
                if (!usesReason(setting.message, vars))
                    alert.message += "\n\n" + builtIn;
                // Pushover refuses more than 1024 characters, and the resend
                // would retry for ever: too long falls back to the built-in text.
                if (alert.message.size() > MAX_CRITICAL_MESSAGE_BYTES)
                    alert.message = builtIn;
            }
            alert.vars.clear();
            alert.vars.shrink_to_fit();
        }

        bool alertGoesOut(Alerts::AlertType type, const AlertsConfig &alerts, bool sending)
        {
            // The roof's critical alerts: past the master switch, the pause,
            // the send mode and the grace (spec 005 FR-014, spec 007 FR-004).
            return Alerts::isCritical(type) || (alerts.enabled && sending);
        }

        Alerts::Alert finishCriticalAlert(Alerts::Alert alert, const AlertSources &sources, const LocalClock &clock)
        {
            const AlertsConfig::EventSetting *setting = eventSettingFor(sources.cfg.alerts, alert.type);
            alert.level = criticalLevel(setting != nullptr ? setting->level : 4);
            if (setting == nullptr)
                return alert;
            // Never silent (spec 007): a silent or vibrate-only sound uses the default.
            alert.sound = AlertsConfig::audibleSound(setting->sound) ? setting->sound : std::string();
            applyCriticalTemplate(alert, *setting, alertVars(sources, alert, clock));
            return alert;
        }

        Alerts::Alert benchLabelled(Alerts::Alert alert, bool bench)
        {
            if (bench && alert.title.rfind(BENCH_ALERT_PREFIX, 0) != 0)
                alert.title = BENCH_ALERT_PREFIX + alert.title;
            return alert;
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

        void addClientInputs(Alerts::AlertInputs &inputs, const ClientSnapshot &clients, const Config &cfg, const std::string &localTime)
        {
            const Alpaca::ClientWatch &watch = clients.watch;
            const std::vector<Alpaca::DeviceInfo> &devices = clients.devices;
            const uint32_t nowMs = clients.nowMs;
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
            JsonObject alpaca,
            const Alpaca::ClientWatch &watch,
            const std::vector<Alpaca::DeviceInfo> &devices,
            const Config &cfg,
            uint32_t nowMs)
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
                {"mount_not_parked", Alerts::AlertType::MountNotParked, "Mount didn't park", "The mount doesn't park"},
                {"roof_not_closed", Alerts::AlertType::RoofNotClosed, "Roof didn't close", "The roof doesn't close"},
                {"move_fault", Alerts::AlertType::MoveFault, "Roof fault", "The roof faults"},
                {"supply_low", Alerts::AlertType::SupplyLow, "12 V supply low", "The 12 V supply is low"},
                {"unsafe_closing", Alerts::AlertType::UnsafeClosing, "Unsafe: closing", "The weather closes the roof"},
                {"roof_closed", Alerts::AlertType::RoofClosed, "Roof closed after unsafe weather", "The roof closed after unsafe weather"},
                {"safety_lost", Alerts::AlertType::SafetyLost, "Weather source lost", "A weather source is lost"},
                {"client_lost", Alerts::AlertType::ClientLost, "Imaging app stopped checking", "The imaging app stops checking"},
                {"client_back", Alerts::AlertType::ClientBack, "Imaging app is back", "The imaging app is back"},
                {"client_disconnected", Alerts::AlertType::ClientDisconnected, "Imaging app disconnected", "The imaging app disconnects"},
                {"daylight_open", Alerts::AlertType::DaylightOpen, "Roof open in daylight", "The roof is open in daylight"},
                {"daylight_closed", Alerts::AlertType::DaylightClosed, "Roof closed after daylight", "The roof closed after daylight"},
                {"daylight_unknown", Alerts::AlertType::DaylightUnknown, "Daylight check not working", "The daylight check can't run"},
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
            const bool critical = Alerts::isCritical(sample->type);
            test.level = critical ? criticalLevel(level) : static_cast<Alerts::AlertLevel>(level);
            test.sound = wording.sound;
            test.title = sample->title;
            test.message = std::string("This is how a \"") + sample->label + "\" alert arrives.";

            // Values only a real event has are examples.
            test.vars = {
                {"device", "RoofRoof Dome (example)"},
                {"silent_for", formatDuration(cfg.alerts.clientSilentSeconds)},
                {"last_checked", "2 min ago (example)"},
                {"client_id", "1234 (example)"},
                {"reason", "Timeout 61.0 s > 60.0 s (example)"},
                {"limit", "240 s (example)"},
                {"details", test.message},
                {"reminder", "no"}};
            AlertsConfig::EventSetting custom{level, wording.sound, wording.title, wording.message};
            if (critical)
                applyCriticalTemplate(test, custom, alertVars(sources, test, clock));
            else
                applyAlertTemplate(test, custom, alertVars(sources, test, clock));
            test.title = "Test: " + test.title;
            return test;
        }
    } // namespace Core
} // namespace SQM
