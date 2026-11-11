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

        Safety::HttpOutcome httpGet(const std::string &url)
        {
            Safety::HttpOutcome reply;
            if (WiFi.status() != WL_CONNECTED)
            {
                reply.error = "WiFi not connected";
                return reply;
            }
            const uint64_t deadline = SafetyPoller::nowMs() + HTTP_TOTAL_MS;
            DualStackClient client;
            HTTPClient http;
            http.setTimeout(HTTP_TIMEOUT_MS);
            http.setConnectTimeout(HTTP_TIMEOUT_MS);
            http.setReuse(false);
            http.useHTTP10(true); // no chunked replies: the body is read as it comes
            if (!beginRequest(http, client, url))
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
            return httpGet(joinUrl(base, request.path));
        }
    } // namespace

    uint64_t SafetyPoller::nowMs()
    {
        return static_cast<uint64_t>(esp_timer_get_time() / 1000);
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
            if (now.sqmeterEnabled && nowMs() >= nextSqmeterMs)
            {
                nextSqmeterMs = nowMs() + SQMETER_POLL_MS;
                pollSqmeter(now, sqmeterGeneration);
            }
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
        const Safety::HttpOutcome reply = httpGet(now.sqmeterUrl);
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.sqmeterGeneration)
            Safety::recordHttpReply(state.sqmeter, reply, nowMs());
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
