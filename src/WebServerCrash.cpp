#include "CrashLog.h"
#include "HeapMap.h"
#include "RoofControl.h"
#include "WebReply.h"
#include "WebServer.h"

// Spec 029 (contracts/crashes.md): GET /api/diag/crashes and GET /api/diag/log. Both reads
// need the password when protection is on (log lines can name hosts and
// topics, constitution VIII) and change nothing; they are built
// from copies (the records from NVS, the ring under its lock).

namespace SQM
{
    namespace
    {
        constexpr const char *CRASHES_PATH = "/api/diag/crashes";
        constexpr const char *LOG_PATH = "/api/diag/log";
        constexpr const char *HEAP_PATH = "/api/diag/heap"; // spec 038
    } // namespace

    void WebServer::setupCrashRoutes()
    {
        // Before /api/diag, which matches by prefix (setupDiagRoutes).
        server.on("/api/diag/crashes", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCrashDocument(request); });
        server.on("/api/diag/log", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCrashDocument(request); });
        server.on("/api/diag/heap", HTTP_GET, [this](AsyncWebServerRequest *request) { handleHeapCensus(request); });
    }

    void WebServer::handleCrashDocument(AsyncWebServerRequest *request)
    {
        const bool crashes = request->url() == CRASHES_PATH;
        if (!crashes && request->url() != LOG_PATH)
        {
            handleNotFound(request);
            return;
        }
        if (!requireAuth(request))
            return;
        // Built from a copy, outside every lock, in 1 KB pieces (spec 031).
        HeapSafe::ChunkText text;
        if (crashes)
            CrashLog::crashesText(text);
        else
            CrashLog::logText(text);
        WebReply::sendText(request, 200, "text/plain; charset=utf-8", std::move(text));
    }

    // Spec 038: the heap census. The walk holds the heap's lock for a few
    // milliseconds, so it isn't run while the roof moves or a close waits.
    void WebServer::handleHeapCensus(AsyncWebServerRequest *request)
    {
        if (request->url() != HEAP_PATH)
        {
            handleNotFound(request);
            return;
        }
        if (!requireAuth(request))
            return;
        if (roof != nullptr && (roof->moving() || roof->closeWaiting()))
        {
            request->send(409, "application/json", "{\"error\":\"The roof is moving: try again once it has stopped\"}");
            return;
        }
        HeapSafe::ChunkText text;
        if (!HeapMap::text(text))
        {
            WebReply::sendBusy(request);
            return;
        }
        WebReply::sendText(request, 200, "text/plain; charset=utf-8", std::move(text));
    }
} // namespace SQM
