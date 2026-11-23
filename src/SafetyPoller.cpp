#include "SafetyPoller.h"

#include "DualStackClient.h"
#include "Logger.h"
#include "NetAddress.h"
#include "SqmeterSource.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_timer.h>

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Safety";
        // Connect and wait-for-reply timeouts (spec 004 FR-010, FR-018).
        constexpr uint16_t HTTP_TIMEOUT_MS = 3000;
        // The body is read until this long after the request started. Connecting
        // (3 s) and each header line (3 s) have their own timeouts, and the DNS
        // lookup none, so a broken server can hold the poller longer; its
        // readings then go stale, which is unsafe (spec 004 FR-010).
        constexpr uint32_t HTTP_TOTAL_MS = 6000;
        // N.I.N.A.'s full mount info is about 4 KB; SQMeter's safety document under 1 KB.
        constexpr size_t MAX_BODY_BYTES = 8192;

        // HTTPClient splits "http://[fd00::10]:8080/x" at the first ':', so IPv6
        // hosts go in by parts (as AlertDispatcher does).
        bool beginRequest(HTTPClient &http, WiFiClient &client, const std::string &url)
        {
            Net::HttpUrl parsed;
            if (Net::parseHttpUrl(url, parsed) == Net::UrlError::None && parsed.host.ipv6)
                return http.begin(client, Net::hostForUrl(parsed.host).c_str(), parsed.port, parsed.path.c_str(), false);
            return http.begin(client, url.c_str());
        }

        // The body, straight into `out`: until its Content-Length (`size`, -1 if
        // not given) or the server closes; false past the cap or the deadline.
        bool readBody(WiFiClient &stream, int size, std::string &out, uint64_t deadlineMs)
        {
            if (size > static_cast<int>(MAX_BODY_BYTES))
                return false;
            out.reserve(size > 0 ? static_cast<size_t>(size) : 1024);
            char chunk[256];
            while (SafetyPoller::nowMs() < deadlineMs)
            {
                if (size >= 0 && out.size() >= static_cast<size_t>(size))
                    return true;
                const int available = stream.available();
                if (available <= 0)
                {
                    if (!stream.connected())
                        return true;
                    delay(5);
                    continue;
                }
                const int got = stream.read(reinterpret_cast<uint8_t *>(chunk), sizeof(chunk));
                if (got > 0)
                    out.append(chunk, static_cast<size_t>(got));
                if (out.size() > MAX_BODY_BYTES)
                    return false;
            }
            return false;
        }

        // The body after the headers. HTTPClient has no stream once the server
        // has closed with nothing left to read (an empty reply): that is an
        // empty body, which then parses as unreadable.
        bool readReply(HTTPClient &http, std::string &out, uint64_t deadlineMs)
        {
            WiFiClient *stream = http.getStreamPtr();
            if (stream == nullptr)
                return true;
            return readBody(*stream, http.getSize(), out, deadlineMs);
        }

        // One GET set up: the timeouts, no reuse, HTTP/1.0 (no chunked replies:
        // the body is read as it comes), and SQMeter's login (spec 020: HTTP
        // Basic on every request when one is set; never logged).
        bool startGet(HTTPClient &http, WiFiClient &client, const Site::Endpoint &to, uint32_t timeoutMs)
        {
            http.setTimeout(static_cast<uint16_t>(timeoutMs));
            http.setConnectTimeout(static_cast<int32_t>(timeoutMs));
            http.setReuse(false);
            http.useHTTP10(true);
            if (!beginRequest(http, client, to.url))
                return false;
            if (to.hasLogin())
                http.setAuthorization(to.username.c_str(), to.password.c_str());
            return true;
        }

        Safety::HttpOutcome httpGet(const Site::Endpoint &to)
        {
            Safety::HttpOutcome reply;
            reply.credentials = to.sendsPassword();
            if (WiFi.status() != WL_CONNECTED)
            {
                reply.error = "WiFi not connected";
                return reply;
            }
            const uint64_t deadline = SafetyPoller::nowMs() + HTTP_TOTAL_MS;
            DualStackClient client;
            HTTPClient http;
            if (!startGet(http, client, to, HTTP_TIMEOUT_MS))
            {
                reply.error = "invalid URL";
                return reply;
            }
            const int code = http.GET();
            if (code <= 0)
                reply.error = HTTPClient::errorToString(code).c_str();
            else if (!readReply(http, reply.body, deadline))
                reply.error = "reply too slow or too long";
            else
                reply.status = code;
            http.end();
            return reply;
        }

        // Spec 020: the socket as ArduinoJson reads it, until a total deadline
        // (a server that drips its reply can't hold the caller longer).
        class DeadlineReader
        {
        public:
            DeadlineReader(WiFiClient &stream, uint64_t deadlineMs)
                : stream(stream),
                  deadlineMs(deadlineMs)
            {
            }

            int read()
            {
                char c = 0;
                return readBytes(&c, 1) == 1 ? static_cast<unsigned char>(c) : -1;
            }

            size_t readBytes(char *buffer, size_t length)
            {
                size_t got = 0;
                while (got < length && SafetyPoller::nowMs() < deadlineMs)
                {
                    if (stream.available() <= 0)
                    {
                        if (!stream.connected())
                            break;
                        delay(5);
                        continue;
                    }
                    const int n = stream.read(reinterpret_cast<uint8_t *>(buffer + got), length - got);
                    if (n > 0)
                        got += static_cast<size_t>(n);
                }
                return got;
            }

        private:
            WiFiClient &stream;
            uint64_t deadlineMs;
        };

        std::string joinUrl(const std::string &base, const std::string &path)
        {
            const bool slash = !base.empty() && base.back() == '/';
            return (slash ? base.substr(0, base.size() - 1) : base) + path;
        }

        // A N.I.N.A. request. Only GET without credentials is sent so far (v2);
        // v3's POST and Basic auth are refused here rather than sent wrongly.
        Safety::HttpOutcome ninaRequest(const std::string &base, const Nina::Request &request)
        {
            if (request.method != Nina::Method::Get || !request.username.empty())
            {
                Safety::HttpOutcome refused;
                refused.error = "request type not supported yet";
                return refused;
            }
            return httpGet(Site::Endpoint{joinUrl(base, request.path), "", ""});
        }
    } // namespace

    uint64_t SafetyPoller::nowMs()
    {
        return static_cast<uint64_t>(esp_timer_get_time() / 1000);
    }

    Site::Fetch SafetyPoller::fetchJson(
        const Site::Endpoint &from, const JsonDocument &filter, JsonDocument &doc, uint32_t connectMs, uint32_t totalMs)
    {
        Site::Fetch fetch;
        fetch.credentials = from.sendsPassword();
        if (WiFi.status() != WL_CONNECTED)
        {
            fetch.error = "WiFi not connected";
            return fetch;
        }
        const uint64_t deadline = nowMs() + totalMs;
        DualStackClient client;
        HTTPClient http;
        if (!startGet(http, client, from, connectMs))
        {
            fetch.error = "invalid URL";
            return fetch;
        }
        const int code = http.GET();
        if (code <= 0)
            fetch.error = HTTPClient::errorToString(code).c_str();
        else
        {
            fetch.status = code;
            WiFiClient *stream = http.getStreamPtr();
            if (code == 200 && stream != nullptr)
            {
                DeadlineReader reader(*stream, deadline);
                fetch.parsed = deserializeJson(doc, reader, DeserializationOption::Filter(filter)) == DeserializationError::Ok;
            }
        }
        http.end();
        return fetch;
    }

    SafetyPoller::SafetyPoller()
        : mutex(xSemaphoreCreateMutex())
    {
    }

    SafetyPoller::~SafetyPoller()
    {
        if (task != nullptr)
            vTaskDelete(task);
        if (mutex != nullptr)
            vSemaphoreDelete(mutex);
    }

    void SafetyPoller::begin(const Targets &newTargets)
    {
        configure(newTargets, Safety::SettingsChange{});
        if (task != nullptr || mutex == nullptr)
            return;
        // Core 0 at low priority: the Wi-Fi core, away from the web server and the loop.
        if (xTaskCreatePinnedToCore(taskEntry, "safetyPoll", TASK_STACK_BYTES, this, 1, &task, 0) != pdPASS)
        {
            task = nullptr;
            Logger::error(TAG, "Could not start the safety poller task; every source will go stale (unsafe)");
        }
    }

    void SafetyPoller::configure(const Targets &newTargets, const Safety::SettingsChange &change)
    {
        if (mutex == nullptr)
            return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        const uint64_t now = nowMs();
        if (change.sqmeterHttp)
        {
            state.sqmeter = Safety::SourceRecord{};
            state.sqmeter.enabled = newTargets.sqmeterEnabled;
            ++state.sqmeterGeneration;
        }
        if (change.nina)
        {
            const auto status = newTargets.ninaUrl.empty() ? Nina::ReadingStatus::NotConfigured : Nina::ReadingStatus::Waiting;
            state.nina = Nina::failedReading(status, "", now);
            state.lastPark = Nina::ParkReply{};
            state.lastParkAtMs = 0;
            ++state.ninaGeneration;
            parkRequested = false;
        }
        targets = newTargets;
        xSemaphoreGive(mutex);
    }

    void SafetyPoller::setSite(const SiteTarget &target)
    {
        if (mutex == nullptr)
            return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (target.address != site.address || target.username != site.username || target.password != site.password ||
            target.followLocation != site.followLocation || target.followSettings != site.followSettings)
        {
            site = target;
            siteStatus = Site::Endpoint{
                target.followLocation ? Site::siteUrl(target.address, Site::STATUS_PATH) : std::string(), target.username, target.password};
            siteSettings = Site::Endpoint{
                target.followSettings ? Site::siteUrl(target.address, Site::CONFIG_PATH) : std::string(), target.username, target.password};
            state.location = Site::LocationRead{};
            state.settings = Site::ConfigRead{};
            ++state.siteGeneration;
        }
        xSemaphoreGive(mutex);
    }

    Site::ConfigRead SafetyPoller::settingsRead() const
    {
        if (mutex == nullptr)
            return Site::ConfigRead{};
        xSemaphoreTake(mutex, portMAX_DELAY);
        Site::ConfigRead copy = state.settings;
        xSemaphoreGive(mutex);
        return copy;
    }

    Site::LocationRead SafetyPoller::locationRead(uint32_t &generation) const
    {
        if (mutex == nullptr)
            return Site::LocationRead{};
        xSemaphoreTake(mutex, portMAX_DELAY);
        Site::LocationRead copy = state.location;
        generation = state.siteGeneration;
        xSemaphoreGive(mutex);
        return copy;
    }

    void SafetyPoller::requestPark()
    {
        parkRequested = true;
    }

    SafetyPoller::Snapshot SafetyPoller::snapshot() const
    {
        if (mutex == nullptr)
            return Snapshot{};
        xSemaphoreTake(mutex, portMAX_DELAY);
        Snapshot copy = state;
        copy.parkPending = parkRequested;
        xSemaphoreGive(mutex);
        return copy;
    }

    SafetyPoller::Targets SafetyPoller::currentTargets(uint32_t &sqmeterGeneration, uint32_t &ninaGeneration) const
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
        Targets copy = targets;
        sqmeterGeneration = state.sqmeterGeneration;
        ninaGeneration = state.ninaGeneration;
        xSemaphoreGive(mutex);
        return copy;
    }

    void SafetyPoller::taskEntry(void *arg)
    {
        static_cast<SafetyPoller *>(arg)->run();
    }

    void SafetyPoller::run()
    {
        std::unique_ptr<Nina::Api> api;
        uint32_t seenSqmeter = 0;
        uint32_t seenNina = 0;
        UBaseType_t lowestStackFree = UINT32_MAX;
        for (;;)
        {
            uint32_t sqmeterGeneration = 0;
            uint32_t ninaGeneration = 0;
            const Targets now = currentTargets(sqmeterGeneration, ninaGeneration);
            if (!api || seenNina != ninaGeneration)
            {
                api = Nina::makeApi(now.ninaApi);
                seenNina = ninaGeneration;
                nextNinaMs = 0; // new settings: poll at once
            }
            if (seenSqmeter != sqmeterGeneration)
            {
                seenSqmeter = sqmeterGeneration;
                nextSqmeterMs = 0;
            }
            // A request is answered once, never kept for later: a park sent
            // long after it was asked for could stop a night's imaging.
            if (parkRequested.exchange(false))
                sendPark(*api, now, ninaGeneration);
            // N.I.N.A. first: its reading must stay fresh for the park check.
            if (!now.ninaUrl.empty() && nowMs() >= nextNinaMs)
            {
                nextNinaMs = nowMs() + NINA_POLL_MS;
                pollNina(*api, now, ninaGeneration);
            }
            const bool safetyPolled = now.sqmeterEnabled && nowMs() >= nextSqmeterMs;
            if (safetyPolled)
            {
                nextSqmeterMs = nowMs() + SQMETER_POLL_MS;
                pollSqmeter(now, sqmeterGeneration);
            }
            maybeReadSite(safetyPolled, now.sqmeterEnabled);
            // Constitution VI: stack headroom is measured on the bench.
            const UBaseType_t stackFree = uxTaskGetStackHighWaterMark(nullptr);
            if (stackFree < lowestStackFree)
            {
                lowestStackFree = stackFree;
                Logger::debug(TAG, "Poller stack low-water: %u bytes free", static_cast<unsigned>(stackFree));
            }
            vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        }
    }

    // Replies are dropped if that part's settings changed while the request ran.
    void SafetyPoller::pollSqmeter(const Targets &now, uint32_t generation)
    {
        const Safety::HttpOutcome reply = httpGet(Site::Endpoint{now.sqmeterUrl, now.sqmeterUsername, now.sqmeterPassword});
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.sqmeterGeneration)
            Safety::recordHttpReply(state.sqmeter, reply, nowMs());
        xSemaphoreGive(mutex);
    }

    void SafetyPoller::maybeReadSite(bool safetyPolled, bool safetyOn)
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
        const Site::Endpoint status = siteStatus;
        const Site::Endpoint settings = siteSettings;
        const uint32_t generation = state.siteGeneration;
        xSemaphoreGive(mutex);
        Site::LocationSchedule::Pass pass;
        pass.nowMs = nowMs();
        pass.enabled = !status.url.empty();
        pass.busy = roofBusy.load();
        pass.safetyPolled = safetyPolled;
        pass.safetyNextMs = safetyOn ? (nextSqmeterMs == 0 ? 1 : nextSqmeterMs) : 0;
        pass.generation = generation;
        if (locationSchedule.due(pass))
        {
            pollLocation(status, generation);
            return; // one site read per pass, so the pass stays within its bound
        }
        pass.enabled = !settings.url.empty();
        if (settingsSchedule.due(pass))
            pollSettings(settings, generation);
    }

    // Only the parts the sync uses are kept, whatever the alert templates hold.
    void SafetyPoller::pollSettings(const Site::Endpoint &from, uint32_t generation)
    {
        DynamicJsonDocument filter(Site::CONFIG_FILTER_BYTES);
        Site::configFilter(filter);
        DynamicJsonDocument doc(Site::CONFIG_DOC_BYTES);
        const Site::Fetch fetch = fetchJson(from, filter, doc, LOCATION_CONNECT_MS, LOCATION_TOTAL_MS);
        const uint64_t at = nowMs();
        const Site::ConfigRead fresh = Site::readConfig(fetch, doc, at);
        settingsSchedule.done(fresh.good(), at);
        // Constitution VI: the heap this read needs is measured on the bench.
        Logger::debug(
            TAG,
            "SQMeter settings read: %s, %u bytes parsed, heap free %u (lowest %u)",
            fresh.good() ? "good" : fresh.detail.c_str(),
            static_cast<unsigned>(doc.memoryUsage()),
            static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMinFreeHeap()));
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.siteGeneration)
            Site::record(state.settings, fresh);
        xSemaphoreGive(mutex);
    }

    // Only sky's three fields are kept, whatever else the status holds.
    void SafetyPoller::pollLocation(const Site::Endpoint &from, uint32_t generation)
    {
        DynamicJsonDocument filter(Site::STATUS_FILTER_BYTES);
        Site::statusFilter(filter);
        DynamicJsonDocument doc(Site::STATUS_DOC_BYTES);
        const Site::Fetch fetch = fetchJson(from, filter, doc, LOCATION_CONNECT_MS, LOCATION_TOTAL_MS);
        const uint64_t at = nowMs();
        const Site::LocationRead fresh = Site::readLocation(fetch, doc, at);
        locationSchedule.done(fresh.good(), at);
        if (!fresh.good())
            Logger::debug(TAG, "SQMeter location not read: %s", fresh.detail.c_str());
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.siteGeneration)
            Site::record(state.location, fresh);
        xSemaphoreGive(mutex);
    }

    void SafetyPoller::pollNina(const Nina::Api &api, const Targets &now, uint32_t generation)
    {
        const Safety::HttpOutcome reply = ninaRequest(now.ninaUrl, api.mountInfoRequest());
        const uint64_t at = nowMs();
        const Nina::MountReading reading = reply.status > 0 ? api.parseMountInfo(reply.status, reply.body, at)
                                                            : Nina::failedReading(Nina::ReadingStatus::Unreachable, reply.error, at);
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.ninaGeneration)
            state.nina = reading;
        xSemaphoreGive(mutex);
    }

    void SafetyPoller::sendPark(const Nina::Api &api, const Targets &now, uint32_t generation)
    {
        Nina::ParkReply parsed{Nina::ParkOutcome::Refused, "N.I.N.A. not configured"};
        if (!now.ninaUrl.empty())
        {
            // v2's park is a GET (research R2).
            const Safety::HttpOutcome reply = ninaRequest(now.ninaUrl, api.parkRequest());
            parsed =
                reply.status > 0 ? api.parsePark(reply.status, reply.body) : Nina::ParkReply{Nina::ParkOutcome::Unreadable, reply.error};
        }
        Logger::info(TAG, "N.I.N.A. park request: %s", parsed.detail.c_str());
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.ninaGeneration)
        {
            state.lastPark = parsed;
            state.lastParkAtMs = nowMs();
        }
        xSemaphoreGive(mutex);
    }
} // namespace SQM
