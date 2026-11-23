#include "Logger.h"
#include "PlatformGuard.h"
#include "RoofControl.h"
#include "Teach.h"
#include "WebServer.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

// Spec 019 (contracts/jog-teach-api.md): the jog and the teach. Every POST
// calls requireAuth and needs the command header (constitution VIII); a jog
// counts as a person's (constitution I). The keep-alive deadline is kept by
// the control step, never here.

namespace SQM
{
    namespace
    {
        void sendAnswer(AsyncWebServerRequest *request, const RoofControl::Outcome &outcome, uint32_t jogId = 0, uint32_t limitMs = 0)
        {
            StaticJsonDocument<512> doc;
            doc["success"] = outcome.accepted;
            doc["state"] = Roof::stateName(outcome.state);
            if (jogId != 0 && outcome.accepted)
            {
                doc["id"] = jogId;
                doc["holdMs"] = Roof::JOG_HOLD_MS;
                doc["keepAliveMs"] = Roof::JOG_KEEPALIVE_MS;
                doc["limitMs"] = limitMs;
            }
            if (!outcome.accepted)
            {
                doc["reason"] = outcome.reason;
                doc["error"] = outcome.text;
            }
            std::string body;
            serializeJson(doc, body);
            request->send(outcome.accepted ? 200 : 409, "application/json", body.c_str());
        }

        const RoofControl::Outcome NOT_RUNNING{false, "notStarted", "The roof controller isn't running", Roof::State::Unknown};
    } // namespace

    void WebServer::setupTeachRoutes()
    {
        AsyncCallbackJsonWebHandler *jog = new AsyncCallbackJsonWebHandler(
            "/api/roof/jog", [this](AsyncWebServerRequest *request, JsonVariant &json) { handleJog(request, json); }, 128);
        jog->setMethod(HTTP_POST);
        server.addHandler(jog);
        AsyncCallbackJsonWebHandler *mark = new AsyncCallbackJsonWebHandler(
            "/api/roof/teach/mark", [this](AsyncWebServerRequest *request, JsonVariant &json) { handleTeachMark(request, json); }, 96);
        mark->setMethod(HTTP_POST);
        server.addHandler(mark);
        AsyncCallbackJsonWebHandler *confirm = new AsyncCallbackJsonWebHandler(
            "/api/roof/teach/confirm",
            [this](AsyncWebServerRequest *request, JsonVariant &json) { handleTeachConfirm(request, json); },
            192);
        confirm->setMethod(HTTP_POST);
        server.addHandler(confirm);
        server.on("/api/roof/teach/discard", HTTP_POST, [this](AsyncWebServerRequest *request) { handleTeachDiscard(request); });
        server.on("/api/roof/teach", HTTP_GET, [this](AsyncWebServerRequest *request) { handleTeachStatus(request); });
    }

    void WebServer::handleJog(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        Calib::JogRequest wanted;
        const char *error = nullptr;
        if (!Calib::parseJog(json, wanted, error))
        {
            request->send(400, "application/json", createErrorJson(error).c_str());
            return;
        }
        if (roof == nullptr)
            return sendAnswer(request, NOT_RUNNING);
        uint32_t id = 0;
        uint32_t limitMs = 0;
        const RoofControl::Outcome outcome =
            roof->jog(wanted, [] { return PlatformGuard::sequence() == RoofIo::SequenceState::Idle; }, id, limitMs);
        sendAnswer(request, outcome, id, limitMs);
    }

    void WebServer::handleTeachMark(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        Calib::Where at = Calib::Where::Closed;
        const char *error = nullptr;
        if (!Calib::parseMark(json, at, error))
        {
            request->send(400, "application/json", createErrorJson(error).c_str());
            return;
        }
        sendAnswer(request, roof != nullptr ? roof->teachMark(at) : NOT_RUNNING);
    }

    // The values the person saw, checked against the proposal, then saved over
    // the current settings through the settings save (held while the roof
    // moves). Once applied, the roof's position follows the taught magnets.
    void WebServer::handleTeachConfirm(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        Calib::TeachValues values;
        const char *error = nullptr;
        if (!Calib::parseConfirm(json, values, error))
        {
            request->send(400, "application/json", createErrorJson(error).c_str());
            return;
        }
        if (roof == nullptr)
            return sendAnswer(request, NOT_RUNNING);
        const RoofControl::Outcome check = roof->teachConfirmable(values);
        if (!check.accepted)
            return sendAnswer(request, check);
        // Spec 021: onto the live settings under the save lock.
        std::string reason;
        bool invalid = false;
        const bool saved = editAsFlashWrite(
            request,
            [&values, &reason, &invalid](const Config &, Config &next)
            {
                next.roof.hallHighEnd = values.highEnd == Roof::End::Open ? RoofConfig::HallEnd::Open : RoofConfig::HallEnd::Closed;
                next.roof.hallHighCounts = values.highCounts;
                next.roof.hallLowCounts = values.lowCounts;
                invalid = !next.validate(&reason);
                return !invalid;
            });
        if (invalid)
            request->send(400, "application/json", createErrorJson(reason.empty() ? "Invalid configuration" : reason.c_str()).c_str());
        if (!saved)
            return;
        roof->teachClear();
        Logger::info(
            TAG,
            "Hall ends taught by a person: high end %s, %u/%u counts",
            Roof::endName(values.highEnd),
            values.highCounts,
            values.lowCounts);
        request->send(200, "application/json", "{\"success\":true}");
    }

    void WebServer::handleTeachDiscard(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        if (roof != nullptr)
            roof->teachClear();
        request->send(200, "application/json", "{\"success\":true}");
    }

    // Readings only, open like /api/status; copied under the roof's lock and
    // written outside it (constitution VI).
    void WebServer::handleTeachStatus(AsyncWebServerRequest *request)
    {
        const Calib::TeachReport report = roof != nullptr ? roof->teachReport() : Calib::TeachReport();
        DynamicJsonDocument doc(Calib::TEACH_DOCUMENT_CAPACITY);
        Calib::writeTeachDocument(doc.to<JsonObject>(), report);
        std::string body;
        serializeJson(doc, body);
        request->send(200, "application/json", body.c_str());
    }
} // namespace SQM
