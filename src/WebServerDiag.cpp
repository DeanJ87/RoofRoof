#include "BenchMode.h"
#include "Diagnostics.h"
#include "Logger.h"
#include "ResetReason.h"
#include "RoofControl.h"
#include "RoofReadings.h"
#include "WebServer.h"
#include <new>
#include <WiFi.h>
#include <ctime>
#include <esp_heap_caps.h>
#include <functional>

// Spec 017, advanced diagnostics (contracts/diagnostics.md): GET /api/diag,
// the move list, one move (JSON or CSV), clearing the log, the /ws/diag stream
// and the MQTT topics. Readers copy (under the roof's lock or the list's mutex)
// and write outside every lock; the list, the move and the CSV go out as
// chunked replies from those copies (research R4). With the mode off every
// document is {"enabled": false} and nothing is streamed or published.

namespace SQM
{
    namespace
    {
        constexpr const char *MOVES_PATH = "/api/diag/moves";

        // A document sent part by part from its generator.
        struct Chunks
        {
            std::function<bool(size_t, std::string &)> part;
            size_t index = 0;
            std::string pending;
            size_t offset = 0;
            bool done = false;

            size_t fill(uint8_t *buffer, size_t maxLen)
            {
                size_t written = 0;
                while (written < maxLen && !done)
                {
                    if (offset == pending.size())
                    {
                        offset = 0;
                        if (!part(index++, pending))
                        {
                            done = true;
                            pending.clear();
                            break;
                        }
                        continue;
                    }
                    const size_t n = std::min(maxLen - written, pending.size() - offset);
                    memcpy(buffer + written, pending.data() + offset, n);
                    offset += n;
                    written += n;
                }
                return written;
            }
        };

        void sendChunked(
            AsyncWebServerRequest *request,
            const char *type,
            std::function<bool(size_t, std::string &)> part,
            const char *disposition = nullptr)
        {
            auto chunks = std::make_shared<Chunks>();
            chunks->part = std::move(part);
            AsyncWebServerResponse *response = request->beginChunkedResponse(
                type, [chunks](uint8_t *buffer, size_t maxLen, size_t) { return chunks->fill(buffer, maxLen); });
            if (disposition != nullptr)
                response->addHeader("Content-Disposition", disposition);
            request->send(response);
        }

        void sendOff(AsyncWebServerRequest *request)
        {
            request->send(200, "application/json", "{\"enabled\":false}");
        }

        Diag::Clock clockNow()
        {
            const time_t epoch = time(nullptr);
            Diag::Clock clock;
            clock.nowMs = millis();
            clock.epoch = static_cast<int64_t>(epoch) >= Core::CLOCK_VALID_EPOCH ? static_cast<uint32_t>(epoch) : 0;
            return clock;
        }

        // "/api/diag/moves/12" -> 12; 0 when it isn't a positive number.
        uint32_t moveIdOf(const String &url)
        {
            const String tail = url.substring(strlen(MOVES_PATH) + 1);
            if (tail.length() == 0 || tail.length() > 10)
                return 0;
            for (size_t i = 0; i < tail.length(); ++i)
                if (!isDigit(tail[i]))
                    return 0;
            const unsigned long long id = strtoull(tail.c_str(), nullptr, 10);
            return id > UINT32_MAX ? 0 : static_cast<uint32_t>(id);
        }
    } // namespace

    bool WebServer::diagOn() const
    {
        return diagnostics != nullptr && diagnostics->enabled() && roof != nullptr;
    }

    void WebServer::setupDiagRoutes()
    {
        // The more specific paths first: handlers match by prefix.
        server.on("/api/diag/moves/clear", HTTP_POST, [this](AsyncWebServerRequest *request) { handleDiagClear(request); });
        server.on("/api/diag/moves", HTTP_GET, [this](AsyncWebServerRequest *request) { handleDiagMoves(request); });
        server.on("/api/diag", HTTP_GET, [this](AsyncWebServerRequest *request) { handleDiagLive(request); });
        wsDiag.onEvent([this](AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *, uint8_t *, size_t)
                       { onDiagWebSocketEvent(client, type); });
        server.addHandler(&wsDiag);
    }

    Diag::SystemLive WebServer::diagSystem() const
    {
        Diag::SystemLive s;
        s.periodMs = RoofIo::CONTROL_PERIOD_MS;
        s.watchdogLimitMs = RoofIo::WATCHDOG_LIMIT_MS;
        s.stepUs = roof->diagStepUs();
        s.maxStepUs = roof->diagMaxStepUs();
        s.passMs = diagnostics->passMs();
        s.maxPassMs = diagnostics->maxPassMs();
        s.freeHeap = ESP.getFreeHeap();
        s.minFreeHeap = ESP.getMinFreeHeap();
        s.largestBlock = static_cast<int64_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        s.stackControl = roof->stackFreeBytes();
        s.stackLoop = loopTaskHandle != nullptr ? static_cast<int64_t>(uxTaskGetStackHighWaterMark(loopTaskHandle)) : Diag::UNKNOWN;
        static TaskHandle_t network = xTaskGetHandle("async_tcp"); // looked up once (it walks the task lists)
        s.stackNetwork = network != nullptr ? static_cast<int64_t>(uxTaskGetStackHighWaterMark(network)) : Diag::UNKNOWN;
        s.rssi = WiFi.status() == WL_CONNECTED ? static_cast<int64_t>(WiFi.RSSI()) : Diag::UNKNOWN;
        s.uptimeS = millis() / 1000;
        s.resetReason = resetReasonText(esp_reset_reason());
        s.simulation = Bench::SIMULATION;
        s.logReady = diagnostics->logReady();
        return s;
    }

    void WebServer::writeDiagLive(HeapSafe::ChunkText &out) const
    {
        Diag::RoofLive live;
        roof->diagLive(live);
        DynamicJsonDocument doc(Diag::LIVE_DOC_CAPACITY);
        if (doc.capacity() == 0)
        {
            out.fail();
            return;
        }
        Diag::writeLive(doc.to<JsonObject>(), live, diagSystem());
        serializeJson(doc, out);
    }

    void WebServer::handleDiagLive(AsyncWebServerRequest *request)
    {
        if (request->url() != "/api/diag")
        {
            handleNotFound(request);
            return;
        }
        if (!diagOn())
        {
            sendOff(request);
            return;
        }
        HeapSafe::ChunkText text;
        writeDiagLive(text);
        WebReply::sendText(request, 200, "application/json", std::move(text));
    }

    void WebServer::sendMoveList(AsyncWebServerRequest *request, const Diag::Clock &clock)
    {
        // Spec 031: the copy is checked (a few KB), "busy" when short.
        std::shared_ptr<Diag::SummaryList> summaries(new (std::nothrow) Diag::SummaryList());
        if (!summaries)
        {
            WebReply::sendBusy(request);
            return;
        }
        diagnostics->copySummaries(*summaries);
        Diag::ListView view;
        view.held.count = roof->diagIds(view.held.ids);
        view.keep = diagnostics->keepSummaries();
        view.clock = clock;
        sendChunked(
            request,
            "application/json",
            [summaries, view](size_t index, std::string &out) { return Diag::listPart(*summaries, view, index, out); });
    }

    void WebServer::handleDiagMoves(AsyncWebServerRequest *request)
    {
        const bool list = request->url() == MOVES_PATH;
        const uint32_t id = list ? 0 : moveIdOf(request->url());
        if (!list && id == 0)
        {
            handleNotFound(request);
            return;
        }
        if (!diagOn())
        {
            if (list)
                sendOff(request);
            else
                request->send(404, "application/json", createErrorJson("Advanced diagnostics is off").c_str());
            return;
        }
        const Diag::Clock clock = clockNow();
        if (list)
        {
            sendMoveList(request, clock);
            return;
        }
        // Spec 031: the copy is checked (a few KB), "busy" when short.
        std::shared_ptr<Diag::Move> move(new (std::nothrow) Diag::Move());
        if (!move)
        {
            WebReply::sendBusy(request);
            return;
        }
        if (!roof->diagMove(id, *move))
        {
            request->send(404, "application/json", createErrorJson("That move isn't held any more").c_str());
            return;
        }
        if (request->hasParam("format") && request->getParam("format")->value() == "csv")
        {
            const String disposition = String("attachment; filename=\"roofroof-move-") + id + ".csv\"";
            sendChunked(
                request,
                "text/csv",
                [move](size_t index, std::string &out) { return Diag::csvPart(*move, index, out); },
                disposition.c_str());
            return;
        }
        sendChunked(
            request,
            "application/json",
            [move, clock](size_t index, std::string &out) { return Diag::movePart(*move, clock, index, out); });
    }

    // A person's command (constitution VIII): the password and the command
    // header; refused while the roof moves or a close waits (FR-014).
    void WebServer::handleDiagClear(AsyncWebServerRequest *request)
    {
        if (request->url() != "/api/diag/moves/clear")
        {
            handleNotFound(request);
            return;
        }
        if (!requireAuth(request) || !requireCommandHeader(request))
            return;
        if (!diagOn())
        {
            sendOff(request);
            return;
        }
        const RoofIo::HoldReason reason = roof->diagClear();
        if (reason != RoofIo::HoldReason::None)
        {
            request->send(409, "application/json", createErrorJson(RoofIo::holdText(reason)).c_str());
            return;
        }
        diagnostics->clearSummaries();
        Logger::info(TAG, "Diagnostics move log cleared");
        request->send(200, "application/json", "{\"success\":true}");
    }

    void WebServer::onDiagWebSocketEvent(AsyncWebSocketClient *client, AwsEventType type)
    {
        if (type == WS_EVT_CONNECT)
        {
            if (!diagOn())
            {
                client->close(); // FR-006: nothing streams while off
                return;
            }
            prepareWebSocketClient(wsDiagClients, client);
            HeapSafe::ChunkText text;
            writeDiagLive(text);
            WebReply::text(client, text);
        }
        else if (type == WS_EVT_DISCONNECT)
        {
            noteWebSocketClosed(wsDiagClients, client);
        }
    }

    // Main loop: every 250 ms while the roof moves, every 2 s otherwise (FR-006).
    void WebServer::broadcastDiag(uint32_t now)
    {
        if (wsDiag.count() == 0)
            return;
        if (!diagOn())
        {
            wsDiag.closeAll();
            return;
        }
        capWebSocketClients(wsDiag);
        if (now - lastDiagBroadcast < Diag::streamIntervalMs(roof->state()))
            return;
        lastDiagBroadcast = now;
        closeStalledClients(wsDiag, wsDiagClients, now);
        if (wsDiag.count() > 0)
        {
            HeapSafe::ChunkText text;
            writeDiagLive(text);
            WebReply::textAll(wsDiag, text);
        }
    }

    // Main loop: <base>/diag every minute and each finished move once, never
    // while the roof is busy (FR-018: spec 008's busy budget is never used).
    void WebServer::publishDiag(uint32_t now)
    {
        const bool active = diagOn() && diagnostics->mqttOn() && mqttClient != nullptr && mqttClient->isConnected();
        if (!active)
        {
            diagMqttStarted = false;
            return;
        }
        if (now - lastDiagMqttCheck < 1000)
            return;
        lastDiagMqttCheck = now;
        if (roofBusyForMqtt())
            return;
        if (diagMqtt.liveDue(now, false))
        {
            Diag::RoofLive live;
            roof->diagLive(live);
            DynamicJsonDocument doc(Diag::LIVE_DOC_CAPACITY);
            Diag::writeLive(doc.to<JsonObject>(), live, diagSystem());
            mqttClient->publishSubtopicJson(Diag::MQTT_LIVE_SUBTOPIC, doc, false);
            diagMqtt.livePublished(now);
        }
        // The list is copied only when it has changed or a summary is still to go.
        const uint32_t revision = diagnostics->summariesRevision();
        if (diagMqttStarted && revision == diagMqttRevision && !diagSummaryPending)
            return;
        auto summaries = std::make_unique<Diag::SummaryList>();
        diagnostics->copySummaries(*summaries);
        diagMqttRevision = revision;
        if (!diagMqttStarted)
        {
            diagMqtt.start(summaries->highestId()); // nothing from before
            diagMqttStarted = true;
        }
        const int next = diagMqtt.nextSummary(*summaries, false);
        diagSummaryPending = next >= 0;
        if (next < 0)
            return;
        const Diag::Summary &summary = summaries->at(static_cast<uint8_t>(next));
        DynamicJsonDocument doc(Diag::SUMMARY_DOC_CAPACITY);
        Diag::writeSummary(doc.to<JsonObject>(), summary, clockNow(), false);
        if (mqttClient->publishSubtopicJson(Diag::MQTT_MOVE_SUBTOPIC, doc, false))
            diagMqtt.summaryPublished(summary.id); // a failed one goes again on the next check
    }
} // namespace SQM
