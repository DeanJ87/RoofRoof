#include "AlertDispatcher.h"
#include "AlertDelivery.h"
#include "AlertRootCA.h"
#include "DualStackClient.h"
#include "HeapGuard.h"
#include "Logger.h"
#include "NetAddress.h"
#include "NetState.h"
#include "TlsLock.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <ctime>
#include <memory>
#include <new>

// The alert task (spec 053, findings F55/F56): it takes the HTTPS jobs the
// main loop queued, critical ones first, and sends each channel's request.
// Before a TLS session it waits, never on another task's time: while the
// roof moves, and while the heap is below the TLS floor (HeapSafe::tlsFits),
// with a back-off (Alerts::SendGate). It runs on core 0 at the lowest
// priority above idle; the roof's control task is on core 1 at 5.

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Alerts";
        constexpr int HTTP_TIMEOUT_MS = 10000;
        constexpr size_t MAX_ERROR_BODY_CHARS = 120;
        constexpr uint32_t TLS_LOCK_WAIT_MS = 30000;
        constexpr uint32_t RETRY_AFTER_MS = 3000;

        int64_t epochNow()
        {
            const time_t now = time(nullptr);
            return now >= 1704067200 ? static_cast<int64_t>(now) : 0;
        }

        uint32_t freeHeap()
        {
            return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
        }

        uint32_t largestBlock()
        {
            return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }

        // Plain http also reaches IPv6 literals and IPv6-only names (SQMeter spec 015).
        std::unique_ptr<WiFiClient> clientFor(const std::string &url, bool insecureTls, const char *rootCa)
        {
            // Spec 035: checked (nullptr short of memory).
            if (url.rfind("https://", 0) != 0)
                return std::unique_ptr<WiFiClient>(new (std::nothrow) DualStackClient());
            std::unique_ptr<WiFiClientSecure> secure(new (std::nothrow) WiFiClientSecure());
            if (!secure)
                return nullptr;
            if (insecureTls)
                secure->setInsecure();
            else
                secure->setCACert(rootCa);
            return secure;
        }

        // HTTPClient splits "http://[fd00::10]:8080/x" at the first ':', so IPv6
        // hosts go in by parts (the bracketed host is the Host header).
        bool beginRequest(HTTPClient &http, WiFiClient &client, const std::string &url)
        {
            Net::HttpUrl parsed;
            if (Net::parseHttpUrl(url, parsed) == Net::UrlError::None && parsed.host.ipv6)
                return http.begin(client, Net::hostForUrl(parsed.host).c_str(), parsed.port, parsed.path.c_str(), parsed.https);
            return http.begin(client, url.c_str());
        }

        // Which certificates an HTTPS request trusts.
        struct TlsTrust
        {
            bool insecure = false; // the webhook's "skip certificate check"
            const char *rootCa = ALERT_ROOT_CA_PEM;
        };

        // A failed reply's text. Pushover (and ntfy) explain failures in
        // JSON: show the message itself rather than truncated raw JSON.
        std::string errorBody(HTTPClient &http)
        {
            String response = http.getString();
            StaticJsonDocument<512> errorDoc;
            if (!deserializeJson(errorDoc, response))
            {
                const char *message = errorDoc["errors"][0] | errorDoc["error"] | static_cast<const char *>(nullptr);
                if (message != nullptr)
                    response = message;
            }
            return response.length() > 0 ? ": " + std::string(response.substring(0, MAX_ERROR_BODY_CHARS).c_str()) : std::string();
        }

        // Spec 053: `freeDuring`, the free heap with the session still open
        // (after the reply, before it is closed): what the session held.
        bool httpPost(const Delivery::HttpRequest &request, const TlsTrust &tls, std::string &detail, uint32_t &freeDuring)
        {
            const std::unique_ptr<WiFiClient> client = clientFor(request.url, tls.insecure, tls.rootCa);
            if (!client)
            {
                detail = "Low memory";
                return false;
            }
            HTTPClient http;
            http.setTimeout(HTTP_TIMEOUT_MS);
            http.setConnectTimeout(HTTP_TIMEOUT_MS);
            if (!beginRequest(http, *client, request.url))
            {
                detail = "Invalid URL";
                return false;
            }
            http.addHeader("Content-Type", request.contentType.c_str());
            for (const auto &header : request.headers)
                http.addHeader(header.first.c_str(), header.second.c_str());

            const std::string &body = request.body;
            const int code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body.data())), body.size());
            freeDuring = freeHeap();
            if (code <= 0)
            {
                detail = HTTPClient::errorToString(code).c_str();
                http.end();
                return false;
            }
            detail = "HTTP " + std::to_string(code);
            const bool ok = code >= 200 && code < 300;
            if (!ok)
                detail += errorBody(http);
            http.end();
            return ok;
        }

        bool isHttps(const std::string &url)
        {
            return url.rfind("https://", 0) == 0;
        }
    } // namespace

    void AlertDispatcher::taskEntry(void *arg)
    {
        static_cast<AlertDispatcher *>(arg)->run();
    }

    bool AlertDispatcher::takeJob(Job *&job)
    {
        portENTER_CRITICAL(&jobsLock);
        const bool got = jobs.pop(job);
        delivering = got;
        portEXIT_CRITICAL(&jobsLock);
        return got;
    }

    void AlertDispatcher::run()
    {
        for (;;)
        {
            if (xSemaphoreTake(wake, pdMS_TO_TICKS(TASK_IDLE_EXIT_MS)) == pdTRUE)
            {
                Job *job = nullptr;
                while (takeJob(job))
                {
                    deliver(*job);
                    delete job;
                    noteStack();
                    delivering = false;
                }
                continue;
            }

            // Idle: exit and give the stack back. If a job raced in after the
            // timeout, take ownership again instead of exiting.
            taskRunning.store(false);
            bool expected = false;
            portENTER_CRITICAL(&jobsLock);
            const bool waiting = !jobs.empty();
            portEXIT_CRITICAL(&jobsLock);
            if (waiting && taskRunning.compare_exchange_strong(expected, true))
            {
                xSemaphoreGive(wake);
                continue;
            }
            Logger::info(
                TAG,
                "Alert task idle: stopped (least stack left %lu B of %lu B)",
                static_cast<unsigned long>(stackLeast.load()),
                static_cast<unsigned long>(TASK_STACK_BYTES));
            vTaskDelete(nullptr);
        }
    }

    // Spec 053: the least stack the task has had left (ESP-IDF counts bytes).
    void AlertDispatcher::noteStack()
    {
        const uint32_t left = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
        const uint32_t least = stackLeast.load();
        if (least == 0 || left < least)
            stackLeast.store(left);
    }

    bool AlertDispatcher::idle() const
    {
        portENTER_CRITICAL(&jobsLock);
        const bool empty = jobs.empty();
        portEXIT_CRITICAL(&jobsLock);
        return !delivering.load() && empty;
    }

    void AlertDispatcher::deliver(const Job &job)
    {
        portENTER_CRITICAL(&jobsLock);
        gate.start(millis());
        portEXIT_CRITICAL(&jobsLock);
        for (AlertChannel channel : {AlertChannel::Pushover, AlertChannel::Ntfy, AlertChannel::Webhook})
            if (job.channelMask & alertChannelBit(channel))
                deliverOne(job, channel);
    }

    void AlertDispatcher::deliverOne(const Job &job, AlertChannel channel)
    {
        const bool tls = channel == AlertChannel::Pushover || (channel == AlertChannel::Ntfy && isHttps(job.cfg.ntfyServer)) ||
                         (channel == AlertChannel::Webhook && isHttps(job.cfg.webhookUrl));
        for (;;)
        {
            if (!NetState::online())
            {
                setStatus(job.recordId, channel, DeliveryStatus::Skipped, "Not connected to a network");
                return;
            }
            if (networkBusy && networkBusy())
            {
                // TLS needs ~40 KB of heap; an OTA download already holds one session.
                setStatus(job.recordId, channel, DeliveryStatus::Skipped, "Firmware update in progress");
                return;
            }
            if (!waitToSend(job, channel, tls))
                return;
            {
                TlsLock::Guard lock(TLS_LOCK_WAIT_MS);
                if (!lock.ok())
                {
                    setStatus(job.recordId, channel, DeliveryStatus::Skipped, "HTTPS busy with another request");
                    return;
                }
                // Spec 053: the other session (an https SQMeter) may have taken the
                // heap while this waited for the lock: checked again, holding it.
                if (mayStart(tls))
                {
                    sendLocked(job, channel, tls);
                    return;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(Alerts::SEND_MOVE_RECHECK_MS));
        }
    }

    bool AlertDispatcher::mayStart(bool tls) const
    {
        const bool fits = tls ? HeapSafe::tlsFits(largestBlock(), freeHeap()) : HeapSafe::alertFits(largestBlock(), freeHeap());
        return fits && !(roofMoving && roofMoving());
    }

    void AlertDispatcher::sendLocked(const Job &job, AlertChannel channel, bool tls)
    {
        std::string detail;
        bool ok = sendMeasured(job, channel, tls, detail);
        if (!ok && detail.rfind("HTTP ", 0) != 0)
        {
            // Connection-level failure (DNS, TCP, TLS memory): one retry, if
            // the heap still has room for it and the roof is still (spec 053).
            vTaskDelay(pdMS_TO_TICKS(RETRY_AFTER_MS));
            if (mayStart(tls))
                ok = sendMeasured(job, channel, tls, detail);
        }
        setStatus(job.recordId, channel, ok ? DeliveryStatus::Sent : DeliveryStatus::Failed, detail.c_str());
        if (!ok)
            Logger::warn(TAG, "%s delivery failed: %s", alertChannelName(channel), detail.c_str());
    }

    // Spec 053: waits (on this task only) until the roof isn't moving and the
    // heap has room for the request: a TLS session's floor for https, the
    // alert path's own (spec 035) for plain http. False: gave up (recorded
    // as skipped for now, so the outbox sends it again).
    bool AlertDispatcher::waitToSend(const Job &job, AlertChannel channel, bool tls)
    {
        Alerts::SendWait shown = Alerts::SendWait::Go;
        for (;;)
        {
            const uint32_t largest = largestBlock();
            const uint32_t free = freeHeap();
            const bool fits = tls ? HeapSafe::tlsFits(largest, free) : HeapSafe::alertFits(largest, free);
            const bool moving = roofMoving && roofMoving();
            portENTER_CRITICAL(&jobsLock);
            const Alerts::SendWait wait = gate.check(millis(), moving, fits);
            const uint32_t retryMs = gate.retryInMs();
            portEXIT_CRITICAL(&jobsLock);
            if (wait == Alerts::SendWait::Go)
                return true;
            if (wait == Alerts::SendWait::GiveUp)
            {
                setStatus(job.recordId, channel, DeliveryStatus::Skipped, Alerts::sendWaitText(wait));
                Logger::warn(TAG, "%s: %s (\"%s\")", alertChannelName(channel), Alerts::sendWaitText(wait), job.alert.title.c_str());
                return false;
            }
            if (wait != shown)
            {
                // Once per reason, not every check: the wait mustn't fill the log.
                shown = wait;
                setStatus(job.recordId, channel, DeliveryStatus::Pending, Alerts::sendWaitText(wait));
                Logger::info(
                    TAG,
                    "%s: %s (largest block %lu B, free %lu B)",
                    alertChannelName(channel),
                    Alerts::sendWaitText(wait),
                    static_cast<unsigned long>(largest),
                    static_cast<unsigned long>(free));
            }
            vTaskDelay(pdMS_TO_TICKS(retryMs));
        }
    }

    // One request, with the heap it found and what the session held logged
    // (spec 053: the owner's bench measures the TLS budget from these lines).
    bool AlertDispatcher::sendMeasured(const Job &job, AlertChannel channel, bool tls, std::string &detail)
    {
        const uint32_t freeBefore = freeHeap();
        const uint32_t largestBefore = largestBlock();
        const uint32_t startedMs = millis();
        uint32_t freeDuring = 0;
        const bool ok = post(job, channel, detail, freeDuring);
        if (tls)
        {
            lastFreeBefore.store(freeBefore);
            lastLargestBefore.store(largestBefore);
            lastFreeDuring.store(freeDuring);
        }
        Logger::info(
            TAG,
            "%s %s in %lu ms: heap free %lu B (largest %lu B) before, %lu B with the session open",
            alertChannelName(channel),
            detail.c_str(),
            static_cast<unsigned long>(millis() - startedMs),
            static_cast<unsigned long>(freeBefore),
            static_cast<unsigned long>(largestBefore),
            static_cast<unsigned long>(freeDuring));
        return ok;
    }

    bool AlertDispatcher::post(const Job &job, AlertChannel channel, std::string &detail, uint32_t &freeDuring)
    {
        if (channel == AlertChannel::Pushover)
        {
            const Delivery::HttpRequest request = Delivery::pushoverRequest(
                job.alert, {job.cfg.pushoverUserKey, job.cfg.pushoverAppToken, job.cfg.pushoverSound}, job.deviceName);
            return httpPost(request, TlsTrust{false, PUSHOVER_ROOT_CA_PEM}, detail, freeDuring);
        }
        if (channel == AlertChannel::Ntfy)
        {
            const Delivery::HttpRequest request =
                Delivery::ntfyRequest(job.alert, {job.cfg.ntfyServer, job.cfg.ntfyTopic, job.cfg.ntfyToken}, job.deviceName);
            const bool ntfySh = request.url.rfind("https://ntfy.sh/", 0) == 0;
            return httpPost(request, TlsTrust{false, ntfySh ? NTFY_SH_ROOT_CA_PEM : ALERT_ROOT_CA_PEM}, detail, freeDuring);
        }
        Delivery::HttpRequest request;
        request.url = job.cfg.webhookUrl;
        request.contentType = "application/json";
        request.body = Delivery::alertJson(job.alert, job.deviceName, epochNow());
        if (!job.cfg.webhookAuthHeader.empty())
            request.headers.push_back({"Authorization", job.cfg.webhookAuthHeader});
        return httpPost(request, TlsTrust{job.cfg.webhookInsecureTls, ALERT_ROOT_CA_PEM}, detail, freeDuring);
    }
} // namespace SQM
