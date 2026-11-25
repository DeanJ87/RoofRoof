#include "Logger.h"
#include "NinaTest.h"
#include "SafetyPoller.h"
#include "SqmeterSecrets.h"
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
        constexpr size_t SITE_DOCUMENT_BYTES = 1792; // spec 024: the switches and the secrets part

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

        WebReply::ErrorText errorBody(const std::string &message)
        {
            return WebReply::errorJson(message.c_str());
        }

        // Spec 031: a job's reply in pieces (the job runs on its own task).
        void setBody(HeapSafe::ChunkText &body, const WebReply::ErrorText &error)
        {
            body.clear();
            body += error.c_str();
        }

        void setBody(HeapSafe::ChunkText &body, const JsonDocument &doc)
        {
            body.clear();
            serializeJson(doc, body);
        }

        // SQMeter's settings, filtered as they arrive; false with the error body.
        bool readSite(const Site::Endpoint &from, Site::SiteConfig &site, HeapSafe::ChunkText &body)
        {
            DynamicJsonDocument filter(Site::CONFIG_FILTER_BYTES);
            Site::configFilter(filter);
            DynamicJsonDocument doc(Site::CONFIG_DOC_BYTES);
            const Site::Fetch fetch = SafetyPoller::fetchJson(
                from, filter, doc, SafetyPoller::FetchLimits{CONNECT_MS, TOTAL_MS, SafetyPoller::TLS_PERSON_WAIT_MS});
            std::string why = "not SQMeter's settings";
            if (fetch.status <= 0)
                setBody(body, errorBody("SQMeter not answering: " + (fetch.error.empty() ? std::string("no reply") : fetch.error)));
            else if (Safety::loginRefusalText(fetch.status, fetch.credentials) != nullptr)
                setBody(body, errorBody(Safety::loginRefusalText(fetch.status, fetch.credentials))); // spec 020: not a generic failure
            else if (fetch.status != 200)
                setBody(body, errorBody("SQMeter answered HTTP " + std::to_string(fetch.status)));
            else if (!fetch.parsed || !Site::readSiteConfig(doc, site, why))
                setBody(body, errorBody("SQMeter's settings couldn't be read: " + why));
            else
                return true;
            return false;
        }

        // Spec 024: SQMeter's secrets, with the same login, at the same address.
        Site::SecretsRead readSecrets(const Site::Endpoint &settings, const std::string &address)
        {
            Site::Endpoint from = settings;
            from.url = Site::siteUrl(address, Site::SECRETS_PATH);
            DynamicJsonDocument filter(Site::SECRETS_FILTER_BYTES);
            Site::secretsFilter(filter);
            DynamicJsonDocument doc(Site::SECRETS_DOC_BYTES);
            const Site::Fetch fetch = SafetyPoller::fetchJson(
                from, filter, doc, SafetyPoller::FetchLimits{CONNECT_MS, TOTAL_MS, SafetyPoller::TLS_PERSON_WAIT_MS});
            return Site::readSecrets(fetch, doc, SafetyPoller::nowMs());
        }

        // Spec 020 FR-038: one read of SQMeter's /api/safety, as the weather
        // poll makes it: 200 with the verdict, or 502 with why not.
        int testSafety(const Site::Endpoint &to, HeapSafe::ChunkText &body)
        {
            StaticJsonDocument<256> out;
            Safety::writeTestResult(out.to<JsonObject>(), SafetyPoller::getOnce(to, SafetyPoller::TLS_PERSON_WAIT_MS));
            setBody(body, out);
            return out["error"].isNull() ? 200 : 502;
        }

        // Spec 027: one read of N.I.N.A.'s mount, as the park poll makes it.
        int testNina(const std::string &url, HeapSafe::ChunkText &body)
        {
            const std::unique_ptr<Nina::Api> api = Nina::makeApi(Nina::ApiVersion::V2);
            const Safety::HttpOutcome reply = SafetyPoller::getOnce(Site::Endpoint{url, "", ""}, SafetyPoller::TLS_PERSON_WAIT_MS);
            StaticJsonDocument<384> out;
            const int status = Nina::writeTestAnswer(
                out.to<JsonObject>(), Nina::testReading(*api, reply.status, reply.body, reply.error, SafetyPoller::nowMs()));
            setBody(body, out);
            return status;
        }
    } // namespace

    struct WebServer::SiteJob
    {
        AsyncWebServerRequestPtr request;
        std::string address;
        Site::Endpoint from; // the URL and the login (never logged)
        bool test = false;   // /api/sqmeter/test: one weather read
        WebServer *owner = nullptr;
        bool savedAddress = false; // spec 024: the secrets are read only at the saved address
        bool nina = false;         // spec 027: /api/nina/test, `from.url` the mount info URL
    };

    // Spec 024: which secrets SQMeter's equal RoofRoof's saved ones, under the
    // save lock (no settings save changes them meanwhile); and the received
    // ones held for the save.
    void WebServer::holdCopied(const Site::SiteConfig &site, const Site::SecretsRead &read, Site::SecretStates &states)
    {
        updateConfigCallback(
            [&](const Config &live, Config &)
            {
                states = Site::secretStates(site.secrets, live);
                return false; // nothing to save
            });
        const std::lock_guard<std::mutex> held(copiedLock);
        copied.reset(read.good() ? new CopiedSecrets(Site::copiedFrom(site)) : nullptr);
        copiedAtMs = millis();
    }

    std::unique_ptr<CopiedSecrets> WebServer::heldCopied()
    {
        const std::lock_guard<std::mutex> held(copiedLock);
        if (copied && millis() - copiedAtMs > Site::SECRETS_HELD_MS)
            copied.reset(); // expired: wiped, not just ignored
        if (!copied)
            return nullptr;
        return std::unique_ptr<CopiedSecrets>(new CopiedSecrets(*copied));
    }

    // SQMeter's settings into the site document, with (spec 024) what became
    // of its secrets; never a secret's value.
    int WebServer::fetchSite(SiteJob &job, HeapSafe::ChunkText &body)
    {
        const auto site = std::unique_ptr<Site::SiteConfig>(new Site::SiteConfig());
        if (!readSite(job.from, *site, body))
            return 502;
        Site::SecretsRead secrets;
        secrets.problem = Site::SecretsProblem::NotSaved;
        if (job.savedAddress)
            secrets = Site::sharingOff(*site) ? Site::sharingOffRead(SafetyPoller::nowMs()) : readSecrets(job.from, job.address);
        site->secrets = secrets.secrets;
        Site::SecretStates states;
        holdCopied(*site, secrets, states);
        DynamicJsonDocument out(SITE_DOCUMENT_BYTES);
        Site::writeSiteDocument(out.to<JsonObject>(), job.address, *site);
        Site::writeSecretsPart(out.as<JsonObject>(), secrets, states, job.address);
        if (out.overflowed())
        {
            setBody(body, errorBody("SQMeter's settings are too long to copy"));
            return 502;
        }
        setBody(body, out);
        return 200;
    }

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
        server.on("/api/nina/test", HTTP_POST, [this](AsyncWebServerRequest *request) { handleNinaTest(request); }, nullptr, collectBody);
    }

    // Spec 027: Settings' N.I.N.A. Test, at the typed address or the saved one.
    void WebServer::handleNinaTest(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        std::string address;
        Site::TypedLogin unused;
        if (!addressFrom(request, getConfigCallback().park.ninaUrl, address, unused))
        {
            request->send(400, "application/json", errorBody("The body must be JSON, at most 512 bytes: {\"url\"}, a string").c_str());
            return;
        }
        if (address.rfind("http://", 0) != 0 && address.rfind("https://", 0) != 0)
        {
            const char *why = address.empty() ? "N.I.N.A.'s address isn't set" : "N.I.N.A.'s address must start with http:// or https://";
            request->send(400, "application/json", errorBody(why).c_str());
            return;
        }
        if (!siteJobAllowed(request))
            return;
        const std::unique_ptr<Nina::Api> api = Nina::makeApi(Nina::ApiVersion::V2);
        auto *job = new SiteJob{request->pause(), address, Site::Endpoint{Nina::testUrl(address, *api), "", ""}, true, this};
        job->nina = true;
        launchSiteJob(job);
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
        if (!siteJobAllowed(request))
            return;
        const char *path = test ? Site::SAFETY_PATH : Site::CONFIG_PATH;
        const SafetyConfig &saved = getConfigCallback().safety;
        launchSiteJob(new SiteJob{
            request->pause(), address, Site::copyEndpoint(saved, address, login, path), test, this, Site::savedAddress(address, saved)});
    }

    // Spec 010: no extra network work while the roof is busy; one read at a time.
    bool WebServer::siteJobAllowed(AsyncWebServerRequest *request)
    {
        const char *why = nullptr;
        bool idle = false;
        if (roofBusy && roofBusy())
            why = "The roof is busy: try again once it is still";
        else if (!siteJobRunning.compare_exchange_strong(idle, true))
            why = "A read is already running: try again in a moment";
        if (why != nullptr)
            request->send(409, "application/json", errorBody(why).c_str());
        return why == nullptr;
    }

    // The job on its own short-lived task (constitution VI).
    void WebServer::launchSiteJob(SiteJob *job)
    {
        if (xTaskCreatePinnedToCore(runSiteJob, "site_job", SITE_JOB_STACK_BYTES, job, 1, nullptr, SITE_JOB_CORE) == pdPASS)
            return;
        const AsyncWebServerRequestPtr paused = job->request;
        delete job;
        siteJobRunning = false;
        if (std::shared_ptr<AsyncWebServerRequest> again = paused.lock())
            again->send(503, "application/json", errorBody("Not enough free memory for the read").c_str());
    }

    void WebServer::runSiteJob(void *arg)
    {
        std::unique_ptr<SiteJob> job(static_cast<SiteJob *>(arg));
        HeapSafe::ChunkText body;
        const int status = job->nina   ? testNina(job->from.url, body)
                           : job->test ? testSafety(job->from, body)
                                       : job->owner->fetchSite(*job, body);
        if (status != 200)
            Logger::info(
                TAG,
                "%s: %s",
                job->nina   ? "Test of N.I.N.A."
                : job->test ? "Test of SQMeter"
                            : "Copy from SQMeter",
                body.str().c_str());
        if (std::shared_ptr<AsyncWebServerRequest> request = job->request.lock())
            WebReply::sendText(request.get(), status, "application/json", std::move(body));
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
