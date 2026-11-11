#include "WebServer.h"
#include "Logger.h"
#include "WebServerShared.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ctime>
#include <esp_attr.h>

// Alerts (engine, schedule, dispatch) and the /api/alerts routes.

namespace SQM
{
    using namespace WebShared;

    namespace
    {
        // The roof conditions already announced (spec 007 FR-012): kept in
        // RTC memory, which survives a reset or a crash but not a power cut,
        // so a board that keeps resetting doesn't repeat the same alert.
        struct KeptAnnounced
        {
            uint32_t magic;
            uint32_t bits;
            uint32_t check;
        };
        constexpr uint32_t KEPT_ANNOUNCED_MAGIC = 0x5EC00701u;
        RTC_NOINIT_ATTR KeptAnnounced keptAnnounced;

        uint32_t announcedCheck(uint32_t bits)
        {
            return (bits ^ 0xA5A5F00Du) * 2654435761u;
        }
    } // namespace

    void WebServer::keepRoofAnnounced()
    {
        if (!roofAnnouncedRestored)
        {
            // Before the engine's first pass after a start.
            roofAnnouncedRestored = true;
            if (keptAnnounced.magic == KEPT_ANNOUNCED_MAGIC && keptAnnounced.check == announcedCheck(keptAnnounced.bits))
                alertEngine.restoreAnnouncedRoof(static_cast<uint8_t>(keptAnnounced.bits));
            return;
        }
        const uint32_t bits = alertEngine.announcedRoof();
        if (keptAnnounced.magic == KEPT_ANNOUNCED_MAGIC && keptAnnounced.bits == bits && keptAnnounced.check == announcedCheck(bits))
            return;
        keptAnnounced.magic = 0;
        keptAnnounced.bits = bits;
        keptAnnounced.check = announcedCheck(bits);
        keptAnnounced.magic = KEPT_ANNOUNCED_MAGIC;
    }

    void WebServer::localClock(std::string &timeText, std::string &dateText)
    {
        char clock[8] = "--:--";
        char date[12] = "--";
        const time_t now = time(nullptr);
        if (now >= Core::CLOCK_VALID_EPOCH)
        {
            struct tm local;
            localtime_r(&now, &local);
            strftime(clock, sizeof(clock), "%H:%M", &local);
            strftime(date, sizeof(date), "%Y-%m-%d", &local);
        }
        timeText = clock;
        dateText = date;
    }

    void WebServer::processAlerts()
    {
        const Config &cfg = getConfigCallback();
        Alerts::AlertInputs in;
        in.nowSeconds = millis() / 1000;
        Core::LocalClock clock;
        localClock(clock.time, clock.date);
        watchClients(in, cfg, clock.time);
        if (roofConditions)
            in.roof = roofConditions();
        keepRoofAnnounced(); // restores before the first pass

        // Always run the engine so its state tracks reality while alerts are
        // off. Each alert gets its configured level, sound and wording;
        // everything raised in the same pass goes out as one notification.
        const time_t wallClock = time(nullptr);
        const uint32_t epoch = wallClock >= Core::CLOCK_VALID_EPOCH ? static_cast<uint32_t>(wallClock) : 0;
        const Core::AlertStep step = Core::runAlerts(alertEngine, in, Core::alertRules(cfg), Core::AlertSources{cfg}, clock);
        keepRoofAnnounced();
        sendAlerts(step, cfg);

        // Only after this pass's alerts went out: a clean disconnect's
        // "imaging app disconnected" is sent before it pauses alerts.
        const bool wasSending = alertSchedule.state().sending;
        if (alertSchedule.update(Core::effectiveSendMode(cfg), alpacaRouter.anyConnected(), millis(), epoch))
            scheduleChanged(wasSending);
        portENTER_CRITICAL(&scheduleLock);
        clientWatchShared = clientWatch;
        portEXIT_CRITICAL(&scheduleLock);
    }

    // The imaging app: is each Alpaca device still being checked?
    void WebServer::watchClients(Alerts::AlertInputs &in, const Config &cfg, const std::string &localTime)
    {
        Alpaca::DeviceActivity activity[Alpaca::MAX_DEVICES];
        for (size_t i = 0; i < Alpaca::MAX_DEVICES; ++i)
            activity[i] = alpacaRouter.activity(i);
        uint32_t silenceMs[Alpaca::MAX_DEVICES];
        Core::clientSilenceMs(cfg, silenceMs);
        clientWatch.update(activity, silenceMs, cfg.alpaca.enabled, millis());
        const std::vector<Alpaca::DeviceInfo> devices = alpacaBackend.devices();
        Core::addClientInputs(in, {clientWatch, devices, millis()}, cfg, localTime);
    }

    // Push channels need the master switch. Paused: state is still tracked,
    // nothing goes out.
    void WebServer::sendAlerts(const Core::AlertStep &step, const Config &cfg)
    {
        if (!alertSchedule.state().sending || step.outgoing.empty() || !cfg.alerts.enabled)
            return;
        alertDispatcher->dispatch(Alerts::stackAlerts(step.outgoing), cfg.alerts, cfg.deviceName, ALERT_CHANNELS_ALL, channelBlocks());
    }

    uint32_t WebServer::raiseRoofAlert(const Alerts::Alert &alert)
    {
        const Config &cfg = getConfigCallback();
        if (!alertDispatcher)
            return 0;
        // Not held by the pause, the send mode, the grace or the master switch:
        // a roof left open in the rain is not a notice the owner chose to
        // silence. Only the channels switched on are used. The settings set
        // the level (never below urgent), sound and wording (spec 007).
        Core::LocalClock clock;
        localClock(clock.time, clock.date);
        const Alerts::Alert finished = Core::finishCriticalAlert(alert, Core::AlertSources{cfg}, clock);
        return alertDispatcher->dispatch(finished, cfg.alerts, cfg.deviceName, ALERT_CHANNELS_ALL, channelBlocks());
    }

    void WebServer::applyPendingArm()
    {
        const int8_t requested = pendingArm.exchange(-1);
        if (requested < 0)
            return;
        const bool wasSending = alertSchedule.state().sending;
        const time_t wallClock = time(nullptr);
        const int64_t epoch = wallClock >= Core::CLOCK_VALID_EPOCH ? static_cast<int64_t>(wallClock) : 0;
        if (alertSchedule.command((requested & 1) != 0, static_cast<Alerts::ScheduleReason>(requested / 2), millis(), epoch))
            scheduleChanged(wasSending);
    }

    void WebServer::shareSchedule()
    {
        portENTER_CRITICAL(&scheduleLock);
        scheduleShared = alertSchedule.state();
        portEXIT_CRITICAL(&scheduleLock);
    }

    Alerts::ScheduleState WebServer::sharedSchedule() const
    {
        portENTER_CRITICAL(&scheduleLock);
        Alerts::ScheduleState state = scheduleShared;
        portEXIT_CRITICAL(&scheduleLock);
        // A pause/resume the loop hasn't applied yet already counts.
        const int8_t pending = pendingArm.load();
        if (pending >= 0)
        {
            state.sending = (pending & 1) != 0;
            state.reason = static_cast<Alerts::ScheduleReason>(pending / 2);
        }
        return state;
    }

    Alpaca::ClientWatch WebServer::sharedClientWatch() const
    {
        portENTER_CRITICAL(&scheduleLock);
        const Alpaca::ClientWatch copy = clientWatchShared;
        portEXIT_CRITICAL(&scheduleLock);
        return copy;
    }

    void WebServer::saveArmedState()
    {
        const Alerts::ScheduleState &state = alertSchedule.state();
        Preferences prefs;
        if (prefs.begin(ARMED_NVS_NAMESPACE, false))
        {
            prefs.putBool("armed", state.sending);
            prefs.putUChar("reason", static_cast<uint8_t>(state.reason));
            prefs.putUInt("since", static_cast<uint32_t>(state.sinceEpoch > 0 ? state.sinceEpoch : 0));
            prefs.end();
        }
        armedSavePending = false;
    }

    void WebServer::scheduleChanged(bool wasSending)
    {
        const Alerts::ScheduleState &state = alertSchedule.state();
        shareSchedule();
        // An NVS write stalls the roof's control task: saved once it stops.
        armedSavePending = true;
        if (!roofMoving())
            saveArmedState();
        if (state.sending == wasSending)
            return;
        Logger::info(TAG, "Alerts %s (%s)", state.sending ? "resumed" : "paused", Alerts::scheduleReasonName(state.reason));
        publishArmedState();

        const Config &cfg = getConfigCallback();
        if (state.sending && cfg.alerts.enabled)
        {
            // One quiet line so you know alerts are going out again. Spec 004
            // adds where things stand (the roof and the weather).
            Alerts::Alert on;
            on.type = Alerts::AlertType::AlertsOn;
            on.level = Alerts::AlertLevel::Quiet;
            on.title = "Alerts resumed";
            on.message = "Alerts from this device are going out again.";
            alertDispatcher->dispatch(on, cfg.alerts, cfg.deviceName, ALERT_CHANNELS_ALL, channelBlocks());
        }
    }

    void WebServer::publishArmedState()
    {
        if (mqttClient == nullptr || !mqttClient->isConnected())
            return;
        if (mqttClient->publishSubtopic("alerts/armed", alertSchedule.state().sending ? "1" : "0", true))
            mqttArmedConnection = mqttClient->connectionCount();
    }

    namespace
    {
        // ?channel= of a test send: the channel bits, false if unknown.
        bool testChannelMask(const String &channel, uint8_t &mask)
        {
            if (channel == "all")
                mask = ALERT_CHANNELS_ALL;
            else if (channel == "mqtt")
                mask = alertChannelBit(AlertChannel::Mqtt);
            else if (channel == "pushover")
                mask = alertChannelBit(AlertChannel::Pushover);
            else if (channel == "ntfy")
                mask = alertChannelBit(AlertChannel::Ntfy);
            else if (channel == "webhook")
                mask = alertChannelBit(AlertChannel::Webhook);
            else
                return false;
            return true;
        }

        uint8_t enabledChannelMask(const AlertsConfig &alerts)
        {
            return (alerts.mqttEnabled ? alertChannelBit(AlertChannel::Mqtt) : 0) |
                   (alerts.pushoverEnabled ? alertChannelBit(AlertChannel::Pushover) : 0) |
                   (alerts.ntfyEnabled ? alertChannelBit(AlertChannel::Ntfy) : 0) |
                   (alerts.webhookEnabled ? alertChannelBit(AlertChannel::Webhook) : 0);
        }

        String paramOr(AsyncWebServerRequest *request, const char *name, const String &fallback)
        {
            return request->hasParam(name) ? request->getParam(name)->value() : fallback;
        }

        // ?event=client_lost&level=4&sound=siren&title=..&message=..: a
        // sample of that event at the given level and wording, so unsaved
        // choices can be tried out.
        struct TestSample
        {
            const Core::SampleAlert *sample = nullptr; // nullptr: the generic test
            uint8_t level = 2;
            String sound;
            String title;
            String message;
        };

        // nullptr when the request is valid, else the error to send.
        const char *readTestSample(AsyncWebServerRequest *request, TestSample &out)
        {
            if (!request->hasParam("event"))
                return nullptr;
            out.sample = Core::sampleAlert(request->getParam("event")->value().c_str());
            if (out.sample == nullptr)
                return "Unknown event";
            out.level = request->hasParam("level") ? static_cast<uint8_t>(request->getParam("level")->value().toInt()) : 2;
            if (out.level < 1 || out.level > 4)
                return "Level must be 1-4";
            out.sound = paramOr(request, "sound", String());
            if (out.sound.length() > 32)
                return "Sound name too long";
            out.title = paramOr(request, "title", String());
            out.message = paramOr(request, "message", String());
            if (!AlertsConfig::templateFits(out.title.c_str(), out.message.c_str()))
                return "Title is up to 80 characters and message up to 240";
            return nullptr;
        }
    } // namespace

    void WebServer::setupAlertRoutes()
    {
        server.on("/api/alerts/test", HTTP_POST, [this](AsyncWebServerRequest *request) { handleAlertTest(request); });
        server.on("/api/alerts/clear", HTTP_POST, [this](AsyncWebServerRequest *request) { handleAlertsClear(request); });

        // Resume ("arm") and pause ("disarm") for automations (Home Assistant
        // rest_command, N.I.N.A. sequence scripts) and the web UI, which adds
        // ?source=ui so the status line can say who paused.
        server.on("/api/alerts/arm", HTTP_POST, [this](AsyncWebServerRequest *request) { handleArm(request, true); });
        server.on("/api/alerts/disarm", HTTP_POST, [this](AsyncWebServerRequest *request) { handleArm(request, false); });
        server.on("/api/alerts/armed", HTTP_GET, [this](AsyncWebServerRequest *request) { handleAlertsArmed(request); });
        server.on("/api/alerts/recent", HTTP_GET, [this](AsyncWebServerRequest *request) { handleAlertsRecent(request); });
    }

    void WebServer::handleAlertTest(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        uint8_t mask = 0;
        if (!testChannelMask(paramOr(request, "channel", String("all")), mask))
        {
            request->send(
                400, "application/json", createErrorJson("Unknown channel (expected mqtt, pushover, ntfy, webhook or all)").c_str());
            return;
        }
        TestSample test;
        if (const char *error = readTestSample(request, test))
        {
            request->send(400, "application/json", createErrorJson(error).c_str());
            return;
        }

        const uint8_t sendMask = mask & enabledChannelMask(getConfigCallback().alerts);
        if (sendMask == 0)
        {
            request->send(
                400, "application/json", createErrorJson("That channel isn't enabled - enable it and save settings first").c_str());
            return;
        }

        portENTER_CRITICAL(&pendingAlertTestLock);
        pendingAlertTest.mask |= sendMask;
        pendingAlertTest.sample = test.sample;
        pendingAlertTest.level = test.level;
        strlcpy(pendingAlertTest.sound, test.sound.c_str(), sizeof(pendingAlertTest.sound));
        strlcpy(pendingAlertTest.title, test.title.c_str(), sizeof(pendingAlertTest.title));
        strlcpy(pendingAlertTest.message, test.message.c_str(), sizeof(pendingAlertTest.message));
        portEXIT_CRITICAL(&pendingAlertTestLock);
        request->send(202, "application/json", "{\"success\":true,\"message\":\"Test notification queued\"}");
    }

    void WebServer::handleAlertsClear(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        alertDispatcher->clearRecent();
        request->send(200, "application/json", "{\"success\":true}");
    }

    void WebServer::handleArm(AsyncWebServerRequest *request, bool armed)
    {
        if (!requireAuth(request))
            return;
        const AsyncWebParameter *source = request->getParam("source");
        const bool fromUi = source != nullptr && source->value() == "ui";
        pendingArm = encodeArm(armed, fromUi ? Alerts::ScheduleReason::UserUi : Alerts::ScheduleReason::UserRest);
        request->send(202, "application/json", armed ? "{\"success\":true,\"armed\":true}" : "{\"success\":true,\"armed\":false}");
    }

    void WebServer::handleAlertsArmed(AsyncWebServerRequest *request)
    {
        StaticJsonDocument<384> doc;
        Core::writeAlertSchedule(doc.to<JsonObject>(), sharedSchedule(), getConfigCallback(), millis());
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    }

    void WebServer::handleAlertsRecent(AsyncWebServerRequest *request)
    {
        const std::vector<AlertRecord> records = alertDispatcher->recent();
        DynamicJsonDocument doc(8192);
        doc["enabled"] = getConfigCallback().alerts.enabled;
        doc["armed"] = sharedSchedule().sending;
        JsonArray arr = doc.createNestedArray("alerts");
        const uint32_t nowSeconds = millis() / 1000;
        for (auto it = records.rbegin(); it != records.rend(); ++it) // newest first
        {
            JsonObject item = arr.createNestedObject();
            item["id"] = it->id;
            item["event"] = Alerts::alertTypeName(it->alert.type);
            item["title"] = it->alert.title;
            item["message"] = it->alert.message;
            item["level"] = Alerts::alertLevelName(it->alert.level);
            item["ageSeconds"] = nowSeconds - it->uptimeSeconds;
            if (it->epochSeconds != 0)
                item["timestamp"] = it->epochSeconds;
            JsonObject channels = item.createNestedObject("channels");
            for (size_t i = 0; i < ALERT_CHANNEL_COUNT; ++i)
            {
                if (it->status[i] == DeliveryStatus::NotSent)
                    continue;
                JsonObject ch = channels.createNestedObject(alertChannelName(static_cast<AlertChannel>(i)));
                ch["status"] = deliveryStatusName(it->status[i]);
                ch["detail"] = it->detail[i];
            }
        }
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    }

} // namespace SQM
