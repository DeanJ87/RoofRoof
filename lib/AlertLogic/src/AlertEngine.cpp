#include "AlertEngine.h"

#include <algorithm>

namespace SQM
{
    namespace Alerts
    {

        namespace
        {
            Alert make(AlertType type, std::string title, std::string message)
            {
                Alert alert;
                alert.type = type;
                alert.title = std::move(title);
                alert.message = std::move(message);
                return alert;
            }

        } // namespace

        const char *alertLevelName(AlertLevel level)
        {
            switch (level)
            {
            case AlertLevel::Quiet:
                return "quiet";
            case AlertLevel::Normal:
                return "normal";
            case AlertLevel::Urgent:
                return "urgent";
            case AlertLevel::Wake:
                return "wake";
            default:
                return "off";
            }
        }

        int pushoverPriority(AlertLevel level)
        {
            switch (level)
            {
            case AlertLevel::Quiet:
                return -1;
            case AlertLevel::Urgent:
                return 1;
            case AlertLevel::Wake:
                return 2;
            case AlertLevel::Normal:
            default:
                return 0;
            }
        }

        const char *ntfyPriority(AlertLevel level)
        {
            switch (level)
            {
            case AlertLevel::Quiet:
                return "low";
            case AlertLevel::Urgent:
                return "high";
            case AlertLevel::Wake:
                return "max";
            case AlertLevel::Normal:
            default:
                return "default";
            }
        }

        const char *alertTypeName(AlertType type)
        {
            // In AlertType order; the names are the event keys in webhook and
            // MQTT payloads and the alert settings.
            static constexpr const char *NAMES[] = {
                "alerts_on",
                "test",
                "client_lost",
                "client_back",
                "client_disconnected",
            };
            static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == ALERT_TYPE_COUNT, "a name for every AlertType");
            const size_t index = static_cast<size_t>(type);
            return index < ALERT_TYPE_COUNT ? NAMES[index] : "unknown";
        }

        std::string joinReasons(const std::vector<std::string> &reasons)
        {
            std::string out;
            for (const std::string &reason : reasons)
            {
                if (!out.empty())
                    out += "\n";
                out += "\u2022 " + reason;
            }
            return out;
        }

        std::string renderTemplate(const std::string &text, const std::vector<std::pair<std::string, std::string>> &vars)
        {
            std::string out;
            out.reserve(text.size() + 32);
            size_t i = 0;
            while (i < text.size())
            {
                const size_t open = text.find('{', i);
                if (open == std::string::npos)
                {
                    out.append(text, i, std::string::npos);
                    break;
                }
                out.append(text, i, open - i);
                const size_t close = text.find('}', open + 1);
                if (close == std::string::npos)
                {
                    out.append(text, open, std::string::npos);
                    break;
                }
                const std::string name = text.substr(open + 1, close - open - 1);
                const std::string *value = nullptr;
                for (const auto &var : vars)
                    if (var.first == name)
                        value = &var.second;
                if (value != nullptr)
                    out += *value;
                else
                    out.append(text, open, close - open + 1);
                i = close + 1;
            }
            return out;
        }

        Alert stackAlerts(const std::vector<Alert> &alerts)
        {
            if (alerts.empty())
                return Alert{};
            size_t lead = 0;
            for (size_t i = 1; i < alerts.size(); ++i)
                if (alerts[i].level > alerts[lead].level)
                    lead = i;
            Alert stacked = alerts[lead];
            if (alerts.size() == 1)
                return stacked;
            stacked.title.clear();
            stacked.message.clear();
            // Lead first, the rest in the order they were raised.
            std::vector<size_t> order{lead};
            for (size_t i = 0; i < alerts.size(); ++i)
                if (i != lead)
                {
                    order.push_back(i);
                    stacked.stacked.push_back(alerts[i].type);
                }
            for (size_t i : order)
            {
                if (!stacked.title.empty())
                    stacked.title += " \u00b7 ";
                stacked.title += alerts[i].title;
                if (!stacked.message.empty())
                    stacked.message += "\n\n";
                stacked.message += alerts[i].message;
            }
            return stacked;
        }

        bool AlertEngine::sync(Tracker &tracker, bool current, const Emit &emit, bool announceAtStart)
        {
            if (!tracker.initialized || !emit.allowed)
            {
                // Follow the condition silently so enabling a rule (or the end
                // of the startup grace) doesn't announce a stale transition.
                tracker.initialized = true;
                tracker.notified = announceAtStart ? false : current;
                tracker.pending = false;
                if (!announceAtStart || !emit.allowed)
                    return false;
            }
            if (current == tracker.notified)
            {
                tracker.pending = false;
                return false;
            }
            if (!tracker.pending)
            {
                tracker.pending = true;
                tracker.pendingSince = emit.now;
            }
            if (emit.now - tracker.pendingSince < emit.settle)
                return false;
            if (tracker.hasNotified && emit.now - tracker.lastNotifiedAt < emit.cooldown)
                return false; // retried on a later update
            tracker.notified = current;
            tracker.pending = false;
            tracker.hasNotified = true;
            tracker.lastNotifiedAt = emit.now;
            return true;
        }

        std::vector<Alert> AlertEngine::update(const AlertInputs &in, const AlertRules &rules)
        {
            std::vector<Alert> alerts;
            if (!started)
            {
                started = true;
                startedAt = in.nowSeconds;
            }
            updateClients(in, rules, alerts);
            return alerts;
        }

        // "Stopped checking" follows the usual cooldown; "is back" answers the
        // "stopped checking" it pairs with straight away (SQMeter spec 021 FR-009), so
        // each one gets exactly one "back" and flapping is still limited by
        // the cooldown on "stopped checking".
        AlertEngine::ClientChange AlertEngine::clientSilence(size_t i, bool silent, bool allowed, uint32_t now, uint32_t cooldown)
        {
            Tracker &tracker = clients[i];
            if (!silent && tracker.initialized && tracker.notified && allowed)
            {
                tracker.notified = false;
                tracker.pending = false;
                const bool answered = lostAnnounced[i];
                lostAnnounced[i] = false;
                return answered ? ClientChange::Back : ClientChange::None;
            }
            if (!silent && !allowed)
                lostAnnounced[i] = false; // back while paused: nothing left to answer
            if (sync(tracker, silent, Emit{now, cooldown, allowed}) && silent)
            {
                lostAnnounced[i] = true;
                return ClientChange::Lost;
            }
            return ClientChange::None;
        }

        void AlertEngine::updateClients(const AlertInputs &in, const AlertRules &rules, std::vector<Alert> &alerts)
        {
            const uint32_t now = in.nowSeconds;
            const uint32_t cooldown = rules.cooldownSeconds;
            for (size_t i = 0; i < in.clientCount && i < MAX_CLIENT_DEVICES; ++i)
            {
                const ClientInputs &client = in.clients[i];
                clientKeys[i] = client.key;
                const std::string device = client.device;
                auto withVars = [&client, &device](Alert alert)
                {
                    // {device} is the Alpaca device here; it overrides the
                    // device name, as an event's own values do.
                    alert.vars = {
                        {"device", device},
                        {"silent_for", client.silentFor},
                        {"last_checked", client.lastChecked},
                        {"client_id", client.clientId}};
                    return alert;
                };

                if (client.disconnectedNow)
                {
                    // A normal end of session: no "stopped checking", and no
                    // "is back" for the request that disconnected.
                    clients[i].initialized = true;
                    clients[i].notified = false;
                    clients[i].pending = false;
                    lostAnnounced[i] = false;
                    disconnectPending[i] = rules.onClientDisconnected;
                }
                else
                {
                    const bool allowed = client.watching && (rules.onClientLost || rules.onClientBack);
                    const ClientChange change = clientSilence(i, client.silent, allowed, now, cooldown);
                    if (change == ClientChange::Lost && rules.onClientLost)
                        alerts.push_back(withVars(make(
                            AlertType::ClientLost,
                            "Imaging app stopped checking",
                            "No request to the " + device + " for " + client.silentFor + " - last checked " + client.lastChecked + ".")));
                    else if (change == ClientChange::Back && rules.onClientBack)
                        alerts.push_back(
                            withVars(make(AlertType::ClientBack, "Imaging app is back", "The " + device + " is being checked again.")));
                }

                if (disconnectPending[i] && (!disconnectSent || now - disconnectSentAt >= cooldown))
                {
                    disconnectPending[i] = false;
                    disconnectSent = true;
                    disconnectSentAt = now;
                    alerts.push_back(
                        withVars(make(AlertType::ClientDisconnected, "Imaging app disconnected", "The " + device + " was disconnected.")));
                }
            }
        }

        const char *waitKindName(WaitKind kind)
        {
            switch (kind)
            {
            case WaitKind::Grace:
                return "grace";
            case WaitKind::Settle:
                return "settle";
            case WaitKind::Cooldown:
                return "cooldown";
            }
            return "settle";
        }

        std::vector<Wait> AlertEngine::waits(uint32_t now, const AlertRules &rules) const
        {
            std::vector<Wait> out;
            if (!started)
                return out;
            const uint32_t sinceStart = now - startedAt;
            if (sinceStart < rules.startupGraceSeconds)
            {
                out.push_back({"startup", WaitKind::Grace, rules.startupGraceSeconds - sinceStart});
                return out;
            }
            // A tracker whose state differs from the one last announced is
            // waiting first to settle, then for its cooldown.
            const auto add = [&](const std::string &name, const Tracker &t, uint32_t settle)
            {
                if (!t.initialized || !t.pending)
                    return;
                const uint32_t held = now - t.pendingSince;
                if (held < settle)
                {
                    out.push_back({name, WaitKind::Settle, settle - held});
                    return;
                }
                const uint32_t since = now - t.lastNotifiedAt;
                if (t.hasNotified && since < rules.cooldownSeconds)
                    out.push_back({name, WaitKind::Cooldown, rules.cooldownSeconds - since});
            };
            for (size_t i = 0; i < MAX_CLIENT_DEVICES; ++i)
                if (!clientKeys[i].empty())
                    add("client:" + clientKeys[i], clients[i], 0);
            return out;
        }

    } // namespace Alerts
} // namespace SQM
