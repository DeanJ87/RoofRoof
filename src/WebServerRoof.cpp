#include "FlashGuard.h"
#include "Logger.h"
#include "MotionGuards.h"
#include "RoofControl.h"
#include "RoofStatus.h"
#include "WebServer.h"
#include <ArduinoJson.h>

// The roof's REST commands and its status section (spec 003 FR-013 to FR-016,
// FR-020; contracts/roof-hardware.md). Every command calls requireAuth
// (constitution VIII) and counts as a person's (constitution I).

namespace SQM
{
    namespace
    {
        // 200 with the new state, or 409 with the refusal's reason and text
        // (constitution III: the measured value and the limit, or the failed input).
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

        const char *commandName(Roof::Command command)
        {
            switch (command)
            {
            case Roof::Command::Open:
                return "open";
            case Roof::Command::Close:
                return "close";
            case Roof::Command::Stop:
                return "stop";
            default:
                return "abort";
            }
        }
    } // namespace

    void WebServer::setupRoofRoutes()
    {
        server.on("/api/roof/open", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofCommand(request, Roof::Command::Open); });
        server.on(
            "/api/roof/close", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofCommand(request, Roof::Command::Close); });
        server.on("/api/roof/stop", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofCommand(request, Roof::Command::Stop); });
        server.on("/api/roof/clear-fault", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRoofClearFault(request); });
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
        Logger::info(TAG, "Roof %s from the web API: %s", commandName(command), outcome.accepted ? "accepted" : "refused");
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
                b.mqttConnecting = mqttClient != nullptr && mqttClient->connecting();
                b.restartScheduled = restartScheduled();
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

    bool WebServer::refuseWhileMoving(AsyncWebServerRequest *request)
    {
        if (!roofMoving())
            return false;
        request->send(409, "application/json", createErrorJson("The roof is moving: stop it first, then try again").c_str());
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

    void WebServer::appendRoof(JsonDocument &doc) const
    {
        if (roof == nullptr)
            return;
        RoofIo::writeStatus(doc.createNestedObject("roof"), roof->snapshot());
    }
} // namespace SQM
