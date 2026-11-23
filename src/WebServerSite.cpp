#include "Logger.h"
#include "SafetyPoller.h"
#include "SqmeterSite.h"
#include <optional>
#include "SqmeterSource.h"
#include "WebServer.h"
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

// Spec 020 (contracts/sqmeter-site-api.md): site data from SQMeter. POST
// /api/sqmeter/site fetches SQMeter's settings for the copy; POST
// /api/sqmeter/test fetches its /api/safety once (Settings' Test, FR-038). Each makes the
// device send a request, so it calls requireAuth (constitution VIII); the
// fetch runs on its own short-lived task, never on the async TCP task
// (constitution VI), and the reply carries no secret's value.

namespace SQM
{
    namespace
    {
        constexpr size_t MAX_BODY_BYTES = 512;
        constexpr uint32_t SITE_JOB_STACK_BYTES = 8192; // a TLS handshake for an https:// SQMeter (FR-039)
        constexpr BaseType_t SITE_JOB_CORE = 1;         // beside the loop task, as the backup job
        constexpr uint32_t CONNECT_MS = 2500;
        constexpr uint32_t TOTAL_MS = 5000;
        constexpr size_t SITE_DOCUMENT_BYTES = 1536;

        void collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
        {
            if (total > MAX_BODY_BYTES)
                return;
            if (index == 0 && request->_tempObject == nullptr)
                request->_tempObject = calloc(total + 1, 1);
            if (request->_tempObject != nullptr && index + len <= total)
                std::memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);
        }

        // A typed string, if the body has one; false when it is there but not a string.
        bool typedString(JsonVariantConst value, std::optional<std::string> &out)
        {
            if (value.isNull())
                return true;
            if (!value.is<const char *>())
                return false;
            out = value.as<const char *>();
            return true;
        }

        // The address to use: the body's "url", else the weather source's;
        // and a typed login ("username", "password"). False when a body was
        // sent but can't be read (too long, not JSON, a field not a string):
        // never another SQMeter's settings by mistake.
        bool addressFrom(AsyncWebServerRequest *request, const std::string &saved, std::string &address, Site::TypedLogin &login)
        {
            address = saved;
            if (request->contentLength() == 0)
                return true;
            if (request->_tempObject == nullptr)
                return false;
            DynamicJsonDocument in(MAX_BODY_BYTES * 2);
            const bool read = !deserializeJson(in, static_cast<const char *>(request->_tempObject)) && in.is<JsonObject>();
            free(request->_tempObject);
            request->_tempObject = nullptr;
            std::optional<std::string> url;
            if (!read || !typedString(in["url"], url) || !typedString(in["username"], login.username) ||
                !typedString(in["password"], login.password))
                return false;
            if (url)
                address = *url;
            return true;
        }

        std::string errorBody(const std::string &message)
        {
            StaticJsonDocument<256> doc;
            doc["error"] = message;
            std::string out;
            serializeJson(doc, out);
            return out;
        }

        // SQMeter's settings, filtered as they arrive, into the site document.
        int fetchSite(const std::string &address, const Site::Endpoint &from, std::string &body)
        {
            DynamicJsonDocument filter(Site::CONFIG_FILTER_BYTES);
            Site::configFilter(filter);
            DynamicJsonDocument doc(Site::CONFIG_DOC_BYTES);
            const Site::Fetch fetch = SafetyPoller::fetchJson(
                from, filter, doc, SafetyPoller::FetchLimits{CONNECT_MS, TOTAL_MS, SafetyPoller::TLS_PERSON_WAIT_MS});
            Site::SiteConfig site;
            std::string why = "not SQMeter's settings";
            if (fetch.status <= 0)
                body = errorBody("SQMeter not answering: " + (fetch.error.empty() ? std::string("no reply") : fetch.error));
            else if (Safety::loginRefusalText(fetch.status, fetch.credentials) != nullptr)
                body = errorBody(Safety::loginRefusalText(fetch.status, fetch.credentials)); // spec 020: not a generic failure
            else if (fetch.status != 200)
                body = errorBody("SQMeter answered HTTP " + std::to_string(fetch.status));
            else if (!fetch.parsed || !Site::readSiteConfig(doc, site, why))
                body = errorBody("SQMeter's settings couldn't be read: " + why);
            else
            {
                DynamicJsonDocument out(SITE_DOCUMENT_BYTES);
                Site::writeSiteDocument(out.to<JsonObject>(), address, site);
                if (out.overflowed())
                {
                    body = errorBody("SQMeter's settings are too long to copy");
                    return 502;
                }
                body.clear();
                serializeJson(out, body);
                return 200;
            }
            return 502;
        }

        // Spec 020 FR-038: one read of SQMeter's /api/safety, as the weather
        // poll makes it: 200 with the verdict, or 502 with why not.
        int testSafety(const Site::Endpoint &to, std::string &body)
        {
            StaticJsonDocument<256> out;
            Safety::writeTestResult(out.to<JsonObject>(), SafetyPoller::getOnce(to, SafetyPoller::TLS_PERSON_WAIT_MS));
            body.clear();
            serializeJson(out, body);
            return out["error"].isNull() ? 200 : 502;
        }
    } // namespace

    struct WebServer::SiteJob
    {
        AsyncWebServerRequestPtr request;
        std::string address;
        Site::Endpoint from; // the URL and the login (never logged)
        bool test = false;   // /api/sqmeter/test: one weather read
    };

    void WebServer::setupSiteRoutes()
    {
        server.on(
            "/api/sqmeter/site",
            HTTP_POST,
            [this](AsyncWebServerRequest *request) { handleSqmeterSite(request, false); },
            nullptr,
            collectBody);
        server.on(
            "/api/sqmeter/test",
            HTTP_POST,
            [this](AsyncWebServerRequest *request) { handleSqmeterSite(request, true); },
            nullptr,
            collectBody);
    }

    void WebServer::handleSqmeterSite(AsyncWebServerRequest *request, bool test)
    {
        if (!requireAuth(request))
            return;
        std::string address;
        Site::TypedLogin login;
        if (!addressFrom(request, getConfigCallback().safety.httpUrl, address, login))
        {
            request->send(
                400,
                "application/json",
                errorBody("The body must be JSON, at most 512 bytes: {\"url\", \"username\", \"password\"}, each a string").c_str());
            return;
        }
        if (address.empty() || Site::siteUrl(address, Site::CONFIG_PATH).empty())
        {
            const char *why =
                address.empty() ? "SQMeter's address isn't set" : "SQMeter's address isn't valid: e.g. 192.168.1.50 or sqmeter.local";
            request->send(400, "application/json", errorBody(why).c_str());
            return;
        }
        // Spec 010: no extra network work while the roof is busy.
        if (roofBusy && roofBusy())
        {
            request->send(409, "application/json", errorBody("The roof is busy: try again once it is still").c_str());
            return;
        }
        bool idle = false;
        if (!siteJobRunning.compare_exchange_strong(idle, true))
        {
            request->send(409, "application/json", errorBody("A read of SQMeter is already running").c_str());
            return;
        }
        const char *path = test ? Site::SAFETY_PATH : Site::CONFIG_PATH;
        auto *job = new SiteJob{request->pause(), address, Site::copyEndpoint(getConfigCallback().safety, address, login, path), test};
        if (xTaskCreatePinnedToCore(runSiteJob, "site_job", SITE_JOB_STACK_BYTES, job, 1, nullptr, SITE_JOB_CORE) != pdPASS)
        {
            const AsyncWebServerRequestPtr paused = job->request;
            delete job;
            siteJobRunning = false;
            if (std::shared_ptr<AsyncWebServerRequest> again = paused.lock())
                again->send(503, "application/json", errorBody("Not enough free memory to read SQMeter").c_str());
        }
    }

    void WebServer::runSiteJob(void *arg)
    {
        std::unique_ptr<SiteJob> job(static_cast<SiteJob *>(arg));
        std::string body;
        const int status = job->test ? testSafety(job->from, body) : fetchSite(job->address, job->from, body);
        if (status != 200)
            Logger::info(TAG, "%s: %s", job->test ? "Test of SQMeter" : "Copy from SQMeter", body.c_str());
        if (std::shared_ptr<AsyncWebServerRequest> request = job->request.lock())
            request->send(status, "application/json", body.c_str());
        job.reset();
        siteJobRunning = false;
        vTaskDelete(nullptr);
    }

    void WebServer::appendSite(JsonDocument &doc) const
    {
        if (siteStatus)
            siteStatus(doc);
    }
} // namespace SQM
