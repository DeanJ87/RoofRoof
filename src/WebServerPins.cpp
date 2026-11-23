#include "ConfigPins.h"
#include "Logger.h"
#include "PinSetup.h"
#include "RoofControl.h"
#include "WebServer.h"
#include "WebServerShared.h"
#include "WiringRun.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

// Spec 023 (contracts/pins-api.md): the pin setup and the wiring check. GET
// /api/pins and POST /api/pins/validate show settings and rules only (no
// secret, nothing changed); POST /api/pins/check reads the inputs and, for a
// moment, EN's pull-down, so it calls requireAuth (constitution VIII) and only
// asks the main loop to run it (constitution VI).

namespace SQM
{
    using namespace WebShared;

    namespace
    {
        constexpr size_t PINS_DOCUMENT_BYTES = 4096;
        constexpr size_t CHECK_DOCUMENT_BYTES = 3072;

        void writeIssues(JsonArray out, const Board::PinReport &report)
        {
            for (const Board::Issue &issue : report.issues)
            {
                JsonObject item = out.createNestedObject();
                if (issue.function == Board::Function::None)
                    item["function"] = nullptr;
                else
                    item["function"] = Board::functionName(issue.function);
                item["pin"] = issue.pin;
                item["code"] = Board::issueCodeName(issue.code);
                item["error"] = issue.error;
                item["text"] = issue.text;
            }
        }

        void writeBoards(JsonArray out)
        {
            for (Board::BoardId board : {Board::BoardId::Esp32Devkit, Board::BoardId::Wt32Eth01})
            {
                const Board::Facts facts = Board::facts(board, false);
                JsonObject item = out.createNestedObject();
                item["board"] = Board::boardName(board);
                JsonArray ethernet = item.createNestedArray("ethernet");
                JsonArray header = item.createNestedArray("header");
                for (unsigned pin = 0; pin < 40; ++pin)
                {
                    if (Board::Facts::has(facts.ethernet, pin))
                        ethernet.add(pin);
                    if (Board::Facts::has(facts.header, pin))
                        header.add(pin);
                }
            }
        }
    } // namespace

    void WebServer::setupPinRoutes()
    {
        // Specific routes first: handlers match by prefix.
        server.on("/api/pins/check", HTTP_POST, [this](AsyncWebServerRequest *request) { handleStartWiringCheck(request); });
        server.on(
            "/api/pins/check",
            HTTP_GET,
            [](AsyncWebServerRequest *request)
            {
                DynamicJsonDocument doc(CHECK_DOCUMENT_BYTES);
                WiringRun::writeDocument(doc);
                std::string out;
                serializeJson(doc, out);
                request->send(200, "application/json", out.c_str());
            });
        AsyncCallbackJsonWebHandler *validate = new AsyncCallbackJsonWebHandler(
            "/api/pins/validate", [this](AsyncWebServerRequest *request, JsonVariant &json) { handleValidatePins(request, json); });
        validate->setMethod(HTTP_POST);
        server.addHandler(validate);
        server.on("/api/pins", HTTP_GET, [this](AsyncWebServerRequest *request) { handleGetPins(request); });
    }

    void WebServer::handleGetPins(AsyncWebServerRequest *request)
    {
        DynamicJsonDocument doc(PINS_DOCUMENT_BYTES);
        const Board::PinMap &running = PinSetup::running();
        const Board::PinMap saved = getConfigCallback().pins;
        ConfigPins::write(doc.createNestedObject("running"), running);
        ConfigPins::write(doc.createNestedObject("saved"), saved);
        doc["restartNeeded"] = saved != running;
        doc["valid"] = PinSetup::valid();
        doc["reason"] = PinSetup::boot().reason;
        writeIssues(doc.createNestedArray("issues"), PinSetup::boot().report);
        doc["psram"] = PinSetup::psram();
        // FR-034: with no board chosen, what the boot probe found (null: it couldn't run).
        if (PinSetup::boardProbed())
            doc["detected"] = Board::boardName(PinSetup::detectedBoard());
        else
            doc["detected"] = nullptr;
        doc["board"] = Board::boardName(PinSetup::board());
        JsonArray presets = doc.createNestedArray("presets");
        for (Board::Preset preset : {Board::Preset::Devkit, Board::Preset::Wt32CarrierV1})
            ConfigPins::write(presets.createNestedObject(), Board::presetMap(preset));
        writeBoards(doc.createNestedArray("boards"));
        std::string out;
        serializeJson(doc, out);
        request->send(200, "application/json", out.c_str());
    }

    void WebServer::handleValidatePins(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!json.is<JsonObject>())
        {
            request->send(400, "application/json", createErrorJson("A pin map must be a JSON object").c_str());
            return;
        }
        Board::PinMap map = getConfigCallback().pins;
        std::string error;
        if (!ConfigPins::read(json.as<JsonObjectConst>(), map, &error))
        {
            request->send(400, "application/json", createErrorJson(error.c_str()).c_str());
            return;
        }
        const Board::PinReport report = Board::check(map, Board::facts(map.board, PinSetup::psram()));
        DynamicJsonDocument doc(PINS_DOCUMENT_BYTES / 2);
        doc["valid"] = report.valid();
        ConfigPins::write(doc.createNestedObject("pins"), map);
        writeIssues(doc.createNestedArray("issues"), report);
        std::string out;
        serializeJson(doc, out);
        request->send(200, "application/json", out.c_str());
    }

    void WebServer::handleStartWiringCheck(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        const std::string refused = WiringRun::start();
        if (!refused.empty())
        {
            request->send(409, "application/json", createErrorJson(refused.c_str()).c_str());
            return;
        }
        request->send(202, "application/json", "{\"started\":true}");
    }

    void WebServer::appendPinsStatus(JsonDocument &doc) const
    {
        JsonObject pins = doc.createNestedObject("pins");
        const Board::PinMap &running = PinSetup::running();
        pins["preset"] = Board::presetName(running.preset);
        pins["board"] = Board::boardName(PinSetup::board());
        pins["valid"] = PinSetup::valid();
        pins["restartNeeded"] = getConfigCallback().pins != running;
    }

    std::string WebServer::pinChangeHeld(const Config &live, const Config &next) const
    {
        if (next.pins == live.pins && next.network.mode == live.network.mode)
            return "";
        const bool busy = (roofBusy && roofBusy()) || (roof != nullptr && (roof->moving() || roof->calibrating()));
        return busy ? "The roof is busy: pins and the network's set-up change only while it is still" : "";
    }
} // namespace SQM
