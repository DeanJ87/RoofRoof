#include "SafetyPoller.h"

#include "DualStackClient.h"
#include "Logger.h"
#include "NetAddress.h"
#include "SqmeterReply.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Safety";
        constexpr int HTTP_OK = 200;

        // The result of one GET: the status (> 0) and body, or why there is none.
        struct HttpReply
        {
            int status = 0;
            std::string body;
            std::string error;
        };

        // HTTPClient splits "http://[fd00::10]:8080/x" at the first ':', so IPv6
        // hosts go in by parts (as AlertDispatcher does).
        bool beginRequest(HTTPClient &http, WiFiClient &client, const std::string &url)
        {
            Net::HttpUrl parsed;
            if (Net::parseHttpUrl(url, parsed) == Net::UrlError::None && parsed.host.ipv6)
                return http.begin(client, Net::hostForUrl(parsed.host).c_str(), parsed.port, parsed.path.c_str(), false);
            return http.begin(client, url.c_str());
        }

        HttpReply httpGet(const std::string &url, uint16_t timeoutMs)
        {
            HttpReply reply;
            if (WiFi.status() != WL_CONNECTED)
            {
                reply.error = "WiFi not connected";
                return reply;
            }
            DualStackClient client;
            HTTPClient http;
            http.setTimeout(timeoutMs);
            http.setConnectTimeout(timeoutMs);
            http.setReuse(false);
            if (!beginRequest(http, client, url))
            {
                reply.error = "invalid URL";
                return reply;
            }
            const int code = http.GET();
            if (code <= 0)
                reply.error = HTTPClient::errorToString(code).c_str();
            else
            {
                reply.status = code;
                reply.body = http.getString().c_str();
            }
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
        HttpReply ninaRequest(const std::string &base, const Nina::Request &request, uint16_t timeoutMs)
        {
            if (request.method != Nina::Method::Get || !request.username.empty())
            {
                HttpReply refused;
                refused.error = "request type not supported yet";
                return refused;
            }
            return httpGet(joinUrl(base, request.path), timeoutMs);
        }

        bool due(uint32_t nextMs, uint32_t nowMs)
        {
            return static_cast<int32_t>(nowMs - nextMs) >= 0;
        }
    } // namespace

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
        configure(newTargets);
        if (task != nullptr || mutex == nullptr)
            return;
        // Core 0 at low priority: the Wi-Fi core, away from the web server and the loop.
        if (xTaskCreatePinnedToCore(taskEntry, "safetyPoll", TASK_STACK_BYTES, this, 1, &task, 0) != pdPASS)
        {
            task = nullptr;
            Logger::error(TAG, "Could not start the safety poller task; every source will go stale (unsafe)");
        }
    }

    void SafetyPoller::configure(const Targets &newTargets)
    {
        if (mutex == nullptr)
            return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        targets = newTargets;
        const uint32_t generation = state.generation + 1;
        state = Snapshot{};
        state.generation = generation;
        state.sqmeter.enabled = newTargets.sqmeterEnabled;
        const uint32_t now = millis();
        state.nina =
            Nina::failedReading(newTargets.ninaUrl.empty() ? Nina::ReadingStatus::NotConfigured : Nina::ReadingStatus::Waiting, "", now);
        nextSqmeterMs = now;
        nextNinaMs = now;
        parkRequested = false;
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

    SafetyPoller::Targets SafetyPoller::currentTargets(uint32_t &generation) const
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
        Targets copy = targets;
        generation = state.generation;
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
        uint32_t apiGeneration = 0;
        UBaseType_t lowestStackFree = UINT32_MAX;
        for (;;)
        {
            uint32_t generation = 0;
            const Targets now = currentTargets(generation);
            if (!api || apiGeneration != generation)
            {
                api = Nina::makeApi(now.ninaApi);
                apiGeneration = generation;
            }
            // A request is answered once, never kept for later: a park sent
            // long after it was asked for could stop a night's imaging.
            if (parkRequested.exchange(false))
                sendPark(*api, now, generation);
            if (!now.ninaUrl.empty() && due(nextNinaMs, millis()))
            {
                nextNinaMs = millis() + NINA_POLL_MS;
                pollNina(*api, now, generation);
            }
            if (now.sqmeterEnabled && due(nextSqmeterMs, millis()))
            {
                nextSqmeterMs = millis() + SQMETER_POLL_MS;
                pollSqmeter(now, generation);
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

    // Results are dropped if the settings changed while the request ran.
    void SafetyPoller::pollSqmeter(const Targets &now, uint32_t generation)
    {
        const HttpReply reply = httpGet(now.sqmeterUrl, HTTP_TIMEOUT_MS);
        Safety::SqmeterReply parsed;
        Safety::ReplyError error = Safety::ReplyError::None;
        if (reply.status == HTTP_OK)
            error = Safety::parseSafetyJson(reply.body, parsed);
        const uint32_t at = millis();
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.generation)
        {
            if (reply.status <= 0)
                state.sqmeter.recordFailure(Safety::Failure::Unreachable, reply.error, at);
            else if (reply.status != HTTP_OK)
                state.sqmeter.recordFailure(Safety::Failure::Unreadable, "HTTP " + std::to_string(reply.status), at);
            else if (error != Safety::ReplyError::None)
                state.sqmeter.recordFailure(Safety::Failure::Unreadable, Safety::replyErrorText(error), at);
            else
                state.sqmeter.recordGood(parsed, at);
        }
        xSemaphoreGive(mutex);
    }

    void SafetyPoller::pollNina(const Nina::Api &api, const Targets &now, uint32_t generation)
    {
        const HttpReply reply = ninaRequest(now.ninaUrl, api.mountInfoRequest(), HTTP_TIMEOUT_MS);
        const uint32_t at = millis();
        const Nina::MountReading reading = reply.status > 0 ? api.parseMountInfo(reply.status, reply.body, at)
                                                            : Nina::failedReading(Nina::ReadingStatus::Unreachable, reply.error, at);
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.generation)
            state.nina = reading;
        xSemaphoreGive(mutex);
    }

    void SafetyPoller::sendPark(const Nina::Api &api, const Targets &now, uint32_t generation)
    {
        Nina::ParkReply parsed{Nina::ParkOutcome::Refused, "N.I.N.A. not configured"};
        if (!now.ninaUrl.empty())
        {
            // v2's park is a GET (research R2).
            const HttpReply reply = ninaRequest(now.ninaUrl, api.parkRequest(), HTTP_TIMEOUT_MS);
            parsed =
                reply.status > 0 ? api.parsePark(reply.status, reply.body) : Nina::ParkReply{Nina::ParkOutcome::Unreadable, reply.error};
        }
        Logger::info(TAG, "N.I.N.A. park request: %s", parsed.detail.c_str());
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.generation)
        {
            state.lastPark = parsed;
            state.lastParkAtMs = millis();
        }
        xSemaphoreGive(mutex);
    }
} // namespace SQM
