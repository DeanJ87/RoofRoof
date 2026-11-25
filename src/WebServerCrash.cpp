#include "CrashLog.h"
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
    } // namespace

    void WebServer::setupCrashRoutes()
    {
        // Before /api/diag, which matches by prefix (setupDiagRoutes).
        server.on("/api/diag/crashes", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCrashDocument(request); });
        server.on("/api/diag/log", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCrashDocument(request); });
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
} // namespace SQM
