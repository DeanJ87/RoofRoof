#include "CrashLog.h"
#include "HeapGuard.h"
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

        // Spec 053: the HTTPS alerts' queue, waits and TLS heap, after the census.
        void appendAlertDelivery(HeapSafe::ChunkText &out, const AlertDeliveryStats &stats)
        {
            char line[200];
            int n = snprintf(
                line,
                sizeof(line),
                "Alert deliveries (HTTPS): queued %lu, dropped (queue full) %lu, waits for memory %lu, waits for a move %lu, gave up %lu\n",
                static_cast<unsigned long>(stats.queued),
                static_cast<unsigned long>(stats.dropped),
                static_cast<unsigned long>(stats.memoryWaits),
                static_cast<unsigned long>(stats.moveWaits),
                static_cast<unsigned long>(stats.gaveUp));
            out.append(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
            n = snprintf(
                line,
                sizeof(line),
                "Alert task: least stack left %lu B of %lu B (0: not run since start); TLS floor: largest block %lu B, free %lu B\n",
                static_cast<unsigned long>(stats.stackLeast),
                static_cast<unsigned long>(AlertDispatcher::TASK_STACK_BYTES),
                static_cast<unsigned long>(HeapSafe::TLS_MIN_LARGEST_BLOCK_BYTES),
                static_cast<unsigned long>(HeapSafe::TLS_MIN_FREE_BYTES));
            out.append(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
            if (stats.lastFreeBefore == 0)
                return;
            n = snprintf(
                line,
                sizeof(line),
                "Last TLS send: free %lu B (largest %lu B) before, %lu B with the session open\n",
                static_cast<unsigned long>(stats.lastFreeBefore),
                static_cast<unsigned long>(stats.lastLargestBefore),
                static_cast<unsigned long>(stats.lastFreeDuring));
            out.append(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
        }
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
        if (alertDispatcher)
            appendAlertDelivery(text, alertDispatcher->deliveryStats());
        WebReply::sendText(request, 200, "text/plain; charset=utf-8", std::move(text));
    }
} // namespace SQM
