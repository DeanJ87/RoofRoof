#include "FlashGuard.h"
#include "Logger.h"
#include "MotionGuards.h"
#include "PlatformGuard.h"
#include "RoofControl.h"
#include "RoofMqtt.h"
#include "RoofStatus.h"
#include "WebServer.h"
#include "WebServerShared.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ctime>

// The roof's REST commands and its status section (spec 003 FR-013 to FR-016,
// FR-020; contracts/roof-hardware.md). Every command calls requireAuth
// (constitution VIII) and counts as a person's (constitution I).

namespace SQM
{
    namespace
    {
        // 200 with the new state, or 409 with the refusal's reason and text
        // (constitution III: the measured value and the limit, or the failed input).
        // And the roof over MQTT (spec 008, contracts/mqtt.md).
        void sendOutcome(AsyncWebServerRequest *request, const RoofControl::Outcome &outcome)
        {
            StaticJsonDocument<512> doc;
            doc["success"] = outcome.accepted;
            doc["state"] = Roof::stateName(outcome.state);
            if (outcome.pending)
            {
                doc["pending"] = true;
                doc["message"] = outcome.text;
            }
            if (!outcome.accepted)
            {
                doc["reason"] = outcome.reason;
                doc["error"] = outcome.text;
            }
            std::string body;
            serializeJson(doc, body);
            const int status = !outcome.accepted ? 409 : (outcome.pending ? 202 : 200);
            request->send(status, "application/json", body.c_str());
        }
    } // namespace

    void WebServer::setupRoofRoutes()
    {
        server.on("/api/roof/open", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofCommand(request, Roof::Command::Open); });
        server.on(
            "/api/roof/close", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofCommand(request, Roof::Command::Close); });
        server.on("/api/roof/stop", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofCommand(request, Roof::Command::Stop); });
        server.on("/api/roof/clear-fault", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofClearFault(request); });
        server.on("/api/roof/inputs", HTTP_GET, [this](AsyncWebServerRequest *request) { handleRoofInputs(request); });
    }

    // Spec 015 (contracts/roof-inputs.md): readings only, open like /api/status.
    // A copy of what the main loop last evaluated: nothing is sent to SQMeter or
    // N.I.N.A. and nothing changes, so it needs no rate limit and never waits on
    // the network or the roof (constitution VI); it waits at most
    // SafetyInputs::REPORT_WAIT_MS for the copy, and answers 503 without it.
    void WebServer::handleRoofInputs(AsyncWebServerRequest *request)
    {
        const std::optional<Safety::InputsReport> report = inputsSource ? inputsSource() : std::nullopt;
        if (!report)
        {
            request->send(503, "application/json", createErrorJson("Inputs not available yet: try again").c_str());
            return;
        }
        DynamicJsonDocument doc(Safety::INPUTS_REPORT_CAPACITY);
        Safety::writeInputsReport(doc.to<JsonObject>(), *report);
        std::string body;
        serializeJson(doc, body);
        request->send(200, "application/json", body.c_str());
    }

    void WebServer::handleRoofCommand(AsyncWebServerRequest *request, Roof::Command command)
    {
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        if (roof == nullptr)
        {
            sendOutcome(request, RoofControl::Outcome{false, "notStarted", "The roof controller isn't running", Roof::State::Unknown});
            return;
        }
        const RoofControl::Outcome outcome = roof->command(command, Roof::Origin::Person);
        Logger::info(TAG, "Roof %s from the web API: %s", Roof::commandName(command), outcome.accepted ? "accepted" : "refused");
        sendOutcome(request, outcome);
    }

    void WebServer::handleRoofClearFault(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        if (roof == nullptr)
        {
            sendOutcome(request, RoofControl::Outcome{false, "notStarted", "The roof controller isn't running", Roof::State::Unknown});
            return;
        }
        sendOutcome(request, roof->clearFault(Roof::Origin::Person));
    }

    void WebServer::setRoof(RoofControl *control)
    {
        roof = control;
        if (roof == nullptr)
            return;
        roof->setBlockers(
            [this]()
            {
                RoofIo::Blockers b;
                b.flashBusy = FlashGuard::busy();
                b.flashWriting = FlashGuard::writing();
                b.restartCommitted = restartCommitted();
                // The setup hotspot has no password: no open while it is on (spec 010 FR-012).
                b.hotspotOn = WebShared::hotspotOn();
                // Bounded in time: a stuck scan or a failed change never holds
                // a close past RoofIo::NETWORK_HOLD_MAX_MS.
                const uint32_t now = millis();
                const bool scanning = wifiScanStarting.load() || WiFi.scanComplete() == WIFI_SCAN_RUNNING;
                b.networkChanging = RoofIo::networkHoldsClose(wifiChanging.load(), now - wifiChangeAtMs.load()) ||
                                    RoofIo::networkHoldsClose(scanning, now - wifiScanAtMs.load());
                b.mqttConnecting = mqttClient != nullptr && mqttClient->connecting();
                b.restartScheduled = restartScheduled();
                b.restartCancellable = restartCancellable();
                return b;
            },
            [](const RoofIo::BlockerDecision &decision)
            {
                if (decision.cancelRestart)
                    cancelRestart();
                if (decision.stopFlash)
                    FlashGuard::requestStop();
            });
    }

    bool WebServer::roofMoving() const
    {
        return roof != nullptr && roof->moving();
    }

    bool WebServer::refuseIfHeld(AsyncWebServerRequest *request, RoofIo::PlatformAction action)
    {
        const RoofIo::HoldReason reason = PlatformGuard::hold(action);
        if (reason == RoofIo::HoldReason::None)
            return false;
        request->send(409, "application/json", createErrorJson(RoofIo::holdText(reason)).c_str());
        return true;
    }

    bool WebServer::requireCommandHeader(AsyncWebServerRequest *request)
    {
        const AsyncWebHeader *header = request->getHeader(RoofIo::COMMAND_HEADER);
        if (RoofIo::commandHeaderValid(header != nullptr ? header->value().c_str() : nullptr))
            return true;
        request->send(403, "application/json", createErrorJson("Roof commands need the header X-RoofRoof: 1").c_str());
        return false;
    }

    RoofIo::Snapshot WebServer::roofSnapshot() const
    {
        RoofIo::Snapshot s = roof->snapshotWithChecks();
        const char *phase = closePhase ? closePhase() : nullptr;
        if (phase != nullptr)
            s.closePhase = phase;
        if (daylightSource)
            s.daylight = daylightSource();
        return s;
    }

    void WebServer::appendRoof(JsonDocument &doc) const
    {
        if (roof == nullptr)
            return;
        const RoofIo::Snapshot snapshot = roofSnapshot();
        JsonObject out = doc.createNestedObject("roof");
        RoofIo::writeStatus(out, snapshot);
        RoofIo::writeDaylight(out, snapshot);
    }

    // Open, close and stop from <base>/roof/set (spec 008 FR-004 to FR-009).
    void WebServer::subscribeRoofCommands()
    {
        if (mqttClient == nullptr)
            return;
        mqttClient->onCommand(
            RoofIo::Mqtt::COMMAND_SUBTOPIC,
            [this](const std::string &payload, RoofIo::Mqtt::Delivery delivery) { onRoofCommand(payload, delivery); });
    }

    // Main loop (PubSubClient's callback). Only a command that passes the
    // setting, the retained check and the payload reaches the roof, and then
    // through RoofControl::command with the MQTT origin: the same checks as
    // every other source, with an Alpaca client's rules (constitution I-III).
    void WebServer::onRoofCommand(const std::string &payload, RoofIo::Mqtt::Delivery delivery)
    {
        const RoofIo::Mqtt::Request request = RoofIo::Mqtt::parseCommand(payload, delivery, getConfigCallback().mqtt.roofCommands);
        RoofIo::Mqtt::Result result;
        result.command = request.isCommand ? Roof::commandName(request.command) : nullptr;
        if (!request.run)
        {
            result.reason = request.reason;
            result.text = request.text;
            result.state = roof != nullptr ? roof->state() : Roof::State::Unknown;
        }
        else
        {
            const RoofControl::Outcome outcome =
                roof != nullptr ? roof->command(request.command, request.origin)
                                : RoofControl::Outcome{false, "notStarted", "The roof controller isn't running", Roof::State::Unknown};
            result.accepted = outcome.accepted;
            result.reason = outcome.reason;
            result.text = outcome.pending ? outcome.text : (outcome.accepted ? std::string() : outcome.text);
            result.state = outcome.state;
        }
        Logger::info(
            TAG,
            "Roof %s from MQTT: %s%s%s",
            result.command != nullptr ? result.command : "command",
            result.accepted ? "accepted" : "refused",
            result.text.empty() ? "" : ": ",
            result.text.c_str());
        DynamicJsonDocument doc(RoofIo::Mqtt::RESULT_DOC_CAPACITY);
        RoofIo::Mqtt::writeResult(doc.to<JsonObject>(), result);
        std::string body;
        serializeJson(doc, body);
        if (pendingRoofResults.size() < 4) // a flood of commands keeps the first answers
            pendingRoofResults.push_back(std::move(body));
    }

    void WebServer::publishRoofResults()
    {
        if (pendingRoofResults.empty())
            return;
        if (mqttClient != nullptr && mqttClient->isConnected())
            for (const std::string &body : pendingRoofResults)
                mqttClient->publishSubtopic(RoofIo::Mqtt::RESULT_SUBTOPIC, body, false);
        pendingRoofResults.clear();
    }

    // The retained roof document on <base>/state, when RoofIo::Mqtt's schedule
    // says (FR-002: nothing periodic while the roof moves or a close waits).
    void WebServer::publishRoofState()
    {
        if (roof == nullptr || mqttClient == nullptr || !mqttClient->isConnected())
            return;
        // Looked at ten times a second, not every loop pass: the snapshot takes the roof's lock.
        const uint32_t passMs = millis();
        if (passMs - lastRoofStateCheck < ROOF_STATE_CHECK_MS)
            return;
        lastRoofStateCheck = passMs;
        const RoofIo::Snapshot snapshot = roofSnapshot();
        const RoofIo::Mqtt::StateKey key = RoofIo::Mqtt::keyOf(snapshot);
        const uint32_t now = millis();
        const uint32_t connection = mqttClient->connectionCount();
        if (!roofStateSchedule.due(key, connection, now, getConfigCallback().mqtt.publishIntervalMs))
            return;
        const time_t epoch = time(nullptr);
        DynamicJsonDocument doc(RoofIo::Mqtt::STATE_DOC_CAPACITY);
        RoofIo::Mqtt::writeState(doc.to<JsonObject>(), snapshot, static_cast<uint32_t>(epoch), epoch >= Core::CLOCK_VALID_EPOCH);
        std::string body;
        serializeJson(doc, body);
        // A failed publish isn't retried at once (the link is going, and the
        // reconnect publishes again); while the roof is busy nothing more is
        // tried on this connection (research R4).
        const bool ok = mqttClient->publishSubtopic(RoofIo::Mqtt::STATE_SUBTOPIC, body, true);
        roofStateSchedule.published(key, connection, now, ok);
    }
} // namespace SQM
