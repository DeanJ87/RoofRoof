#include "WebServer.h"
#include "AlpacaDiscovery.h"
#include "CaptivePortal.h"
#include "HeapTrace.h"
#include "Ipv6Network.h"
#include "Logger.h"
#include "MotionGuards.h"
#include "PlatformGuard.h"
#include "RoofControl.h"
#include "RoofReadings.h"
#include "WebServerShared.h"
#include "WiFiManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_timer.h>

// The web server's core: start-up, the main-loop pass, static files and the
// captive portal. Routes and documents are in WebServerApi.cpp,
// WebServerAlerts.cpp, WebServerAlpaca.cpp, WebServerBackup.cpp,
// WebServerLive.cpp, WebServerStatus.cpp and WebServerUpdates.cpp.

namespace SQM
{
    using namespace WebShared;

    namespace
    {
        // Where captive-portal probes land: the WiFi setup screen on the hotspot.
        std::string setupScreenUrl()
        {
            return std::string("http://") + WiFi.softAPIP().toString().c_str() + "/wifi";
        }

        // "1"/"0", "on"/"off", "true"/"false", "arm"/"disarm" (any case).
        bool parseArmPayload(std::string text, bool &armed)
        {
            while (!text.empty() && isspace(static_cast<unsigned char>(text.back())))
                text.pop_back();
            for (char &c : text)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (text == "1" || text == "on" || text == "true" || text == "arm" || text == "armed")
                armed = true;
            else if (text == "0" || text == "off" || text == "false" || text == "disarm" || text == "disarmed")
                armed = false;
            else
                return false;
            return true;
        }

        esp_timer_handle_t restartTimer = nullptr;

        // Whether a restart is due, whether a close may cancel it and whether
        // it is committed, guarded by restartPlanLock (the web task, the loop,
        // the roof task and the timer task use it). Taken after the roof's
        // lock where both are held, never before it.
        portMUX_TYPE restartPlanLock = portMUX_INITIALIZER_UNLOCKED;
        RoofIo::RestartPlan restartPlan;

        // How soon a held restart is tried again (spec 010). The timer task
        // never waits for the roof's lock, which the roof's watchdog timer
        // shares the task with.
        constexpr uint64_t RESTART_RETRY_US = 200ULL * 1000ULL;

        void restartTimerCallback(void *)
        {
            portENTER_CRITICAL(&restartPlanLock);
            const bool due = restartPlan.scheduled();
            const bool mandatory = restartPlan.mandatory();
            portEXIT_CRITICAL(&restartPlanLock);
            if (!due)
                return; // cancelled: don't re-arm (a new schedule starts the timer itself)
            // Committed under the roof's lock: no move starts after this, and
            // none is under way (spec 010 FR-002, FR-008).
            const PlatformGuard::RestartGo go = PlatformGuard::commitRestart(
                mandatory,
                []()
                {
                    portENTER_CRITICAL(&restartPlanLock);
                    const bool committed = restartPlan.commit();
                    portEXIT_CRITICAL(&restartPlanLock);
                    return committed;
                });
            static bool heldLogged = false;
            if (go == PlatformGuard::RestartGo::Held && restartTimer != nullptr)
            {
                if (!heldLogged)
                    Logger::info("WebServer", "Restart waiting: the roof, a write, a latched fault not yet saved or the automatic close");
                heldLogged = true;
                esp_timer_start_once(restartTimer, RESTART_RETRY_US);
                return;
            }
            heldLogged = false;
            if (go == PlatformGuard::RestartGo::Run)
                ESP.restart();
        }

    } // namespace

    // The request came in over the setup hotspot (not the home network): it
    // may be sent to the setup screen, and it may not open the roof (spec 010).
    bool WebShared::hotspotOn()
    {
        return (WiFi.getMode() & WIFI_AP) != 0;
    }

    bool WebShared::arrivedViaHotspot(AsyncWebServerRequest *request)
    {
        if ((WiFi.getMode() & WIFI_AP) == 0)
            return false;
        AsyncClient *client = request->client();
        return client != nullptr && client->localIP() == WiFi.softAPIP();
    }

    WebServer::WebServer(TimeManager *timeMgr, MQTTClient *mqtt, GetConfigCallback getConfig, SaveConfigCallback saveConfig)
        : server(PORT),
          wsStatus("/ws/status"),
          timeManager(timeMgr),
          mqttClient(mqtt),
          getConfigCallback(getConfig),
          // A save bumps configRevision (/api/status): open pages reload the settings.
          saveConfigCallback(
              [this, saveConfig](const Config &cfg)
              {
                  const bool saved = saveConfig(cfg);
                  if (saved)
                      ++configRevision;
                  return saved;
              })
    {
        // Alerts wait while an upload is written: TLS and flash writes together
        // stall the upload (SQMeter spec 012 FR-003a).
        alertDispatcher = std::make_unique<AlertDispatcher>(mqttClient, [] { return uploadActive.load(); });
    }

    WebServer::~WebServer()
    {
        wsStatus.closeAll();
    }

    void WebServer::begin()
    {
        loopTaskHandle = xTaskGetCurrentTaskHandle();
        restoreAlertSchedule();
        subscribeArmCommands();
        subscribeRoofCommands();
        Logger::info(TAG, "Starting web server on port %d", PORT);

        // CRITICAL: Register API routes BEFORE static file serving
        // Otherwise /api/* requests get treated as filesystem paths
        restoreAlpacaConnections();
        setupAPIRoutes();
        setupWebSocket();
        setupOTA();
        setupAlpacaRoutes();
        setupAlertRoutes();
        setupStaticRoutes(); // Must be last - has catch-all serveStatic

        HeapTrace::mark("web server routes");
        alertDispatcher->begin();
        HeapTrace::mark("alert dispatcher");
        if (getConfigCallback().alpaca.enabled)
            startAlpacaDiscovery();

        // SPA fallback - serve index.html for any non-API routes
        server.onNotFound([](AsyncWebServerRequest *request) { handleNotFound(request); });

        // Not server.begin(): the device owns the listener (Ipv6Network::listen).
        Ipv6Network::listen(server, PORT);
        Logger::info(TAG, "Web server started");
    }

    void WebServer::restoreAlertSchedule()
    {
        Preferences prefs;
        if (prefs.begin(ARMED_NVS_NAMESPACE, true))
        {
            // No "reason" key: saved by firmware from before reasons.
            alertSchedule.restore(
                prefs.getBool("armed", true),
                prefs.isKey("reason"),
                prefs.getUChar("reason", 0),
                static_cast<int64_t>(prefs.getUInt("since", 0)));
            prefs.end();
        }
        shareSchedule();
    }

    // Home Assistant MQTT switch: command <topic>/alerts/armed/set, state
    // <topic>/alerts/armed.
    void WebServer::subscribeArmCommands()
    {
        if (mqttClient == nullptr)
            return;
        mqttClient->onCommand(
            "alerts/armed/set",
            [this](const std::string &payload, RoofIo::Mqtt::Delivery delivery)
            {
                // A retained pause would replay on every reconnect (spec 008):
                // only a live message changes it.
                if (delivery != RoofIo::Mqtt::Delivery::Live)
                {
                    Logger::warn(TAG, "Ignored a retained alerts/armed/set message");
                    return;
                }
                bool armed = false;
                if (parseArmPayload(payload, armed))
                    pendingArm = encodeArm(armed, Alerts::ScheduleReason::UserMqtt);
            });
    }

    void WebServer::startAlpacaDiscovery()
    {
        // With IPv6 on, one dual-stack socket takes IPv4 broadcasts and the
        // IPv6 discovery group (SQMeter spec 015, FR-005).
        const bool ipv6 = WiFiManager::ipv6Running();
        const bool listening = ipv6 ? alpacaDiscoveryUdp.listen(IP_ANY_TYPE, Alpaca::DISCOVERY_UDP_PORT)
                                    : alpacaDiscoveryUdp.listen(Alpaca::DISCOVERY_UDP_PORT);
        alpacaIpv6Pending = listening && ipv6;
        if (!listening)
        {
            Logger::error(TAG, "Failed to start Alpaca UDP discovery listener");
            return;
        }
        // Replies straight from the UDP task: discovery answers within
        // milliseconds instead of waiting for a main-loop pass.
        alpacaDiscoveryUdp.onPacket(
            [](AsyncUDPPacket &packet)
            {
                if (!Alpaca::isValidDiscoveryRequest(packet.data(), packet.length()) || !Ipv6Network::allowedDiscoveryPeer(packet))
                    return;
                const std::string response = Alpaca::buildDiscoveryResponse(PORT);
                packet.write(reinterpret_cast<const uint8_t *>(response.data()), response.size());
            });
        Logger::info(TAG, "Alpaca UDP discovery listening on port %u", Alpaca::DISCOVERY_UDP_PORT);
    }

    void WebServer::handleNotFound(AsyncWebServerRequest *request)
    {
        const String path = request->url();
        const bool hostIsDevice = request->host() == WiFi.softAPIP().toString();
        switch (CaptivePortal::notFound(path.c_str(), arrivedViaHotspot(request), hostIsDevice))
        {
        case CaptivePortal::NotFound::AlpacaError:
            // Alpaca device API: the spec requires HTTP 400 with a plain-text
            // body for an unknown device type/number, method, or HTTP verb.
            Logger::debug(TAG, "400 Invalid Alpaca request: %s %s", request->methodToString(), path.c_str());
            request->send(400, "text/plain", "Invalid Alpaca device type, device number, method or HTTP verb");
            return;
        case CaptivePortal::NotFound::SetupScreen:
            // On the hotspot every hostname resolves here; other sites' pages
            // go to the setup screen. Never over the home network.
            request->redirect(setupScreenUrl().c_str());
            return;
        case CaptivePortal::NotFound::ApiError:
            Logger::debug(TAG, "404 Not Found (API): %s", path.c_str());
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
            return;
        case CaptivePortal::NotFound::FileMissing:
            // A file the app asked for that isn't there: a plain 404, not the
            // app's page.
            request->send(404, "text/plain", "Not found");
            return;
        case CaptivePortal::NotFound::AppPage:
            Logger::debug(TAG, "SPA fallback for: %s", path.c_str());
            request->send(LittleFS, "/index.html", "text/html");
            return;
        }
    }

    void WebServer::retryAlpacaIpv6Discovery(uint32_t now)
    {
        if (!alpacaIpv6Pending || now - lastAlpacaIpv6Attempt < 5000)
            return;
        lastAlpacaIpv6Attempt = now;
        alpacaIpv6Pending = !Ipv6Network::joinAlpacaDiscoveryGroup();
    }

    // Sends the test alert an HTTP handler queued (loop task).
    void WebServer::sendPendingAlertTest()
    {
        PendingAlertTest pendingTest;
        portENTER_CRITICAL(&pendingAlertTestLock);
        pendingTest = pendingAlertTest;
        pendingAlertTest.mask = 0;
        portEXIT_CRITICAL(&pendingAlertTestLock);
        if (pendingTest.mask == 0)
            return;

        const Config &cfg = getConfigCallback();
        Core::LocalClock clock;
        localClock(clock.time, clock.date);
        const Core::TestWording wording{pendingTest.level, pendingTest.sound, pendingTest.title, pendingTest.message};
        const Alerts::Alert test = Core::buildTestAlert(pendingTest.sample, wording, Core::AlertSources{cfg}, clock);
        alertDispatcher->dispatch(test, cfg.alerts, cfg.deviceName, pendingTest.mask, channelBlocks());
    }

    void WebServer::handle()
    {
        capWebSocketClients(wsStatus);
        rememberAlpacaConnections();
        pollWiFiConnect();

        const uint32_t now = millis();

        applyPendingArm();
        if (armedSavePending && !roofMoving())
            saveArmedState();
        retryAlpacaIpv6Discovery(now);
        if (mqttClient != nullptr && mqttClient->connectionCount() != mqttArmedConnection)
            publishArmedState();
        publishDiscovery();
        publishRoofResults();
        publishRoofState();

        if (now - lastAlertPass >= ALERT_PASS_INTERVAL_MS)
        {
            processAlerts();
            lastAlertPass = now;
        }

        sendPendingAlertTest();

        // Status every 2 seconds, and at once when the recent alerts or the
        // settings change, or (at most every 250 ms) the roof's state or fault
        // or the automatic close's phase (spec 008).
        const uint32_t alertsRevision = alertDispatcher ? alertDispatcher->recentRevision() : 0;
        const uint32_t roofRevision = roof != nullptr ? roof->revision() : 0;
        const char *phase = closePhase ? closePhase() : nullptr;
        const bool roofChanged =
            (roofRevision != lastRoofRevision || phase != lastClosePhase) && now - lastRoofBroadcast >= RoofIo::STATUS_BROADCAST_MIN_MS;
        const bool changed = alertsRevision != lastAlertsRevision || configRevision != lastConfigRevision || roofChanged;
        if (now - lastStatusBroadcast >= WS_STATUS_BROADCAST_INTERVAL_MS || changed)
        {
            broadcastStatusData();
            lastStatusBroadcast = now;
            lastRoofBroadcast = now;
            lastAlertsRevision = alertsRevision;
            lastConfigRevision = configRevision;
            lastRoofRevision = roofRevision;
            lastClosePhase = phase;
        }
    }

    bool WebServer::alertsIdle() const
    {
        return alertDispatcher == nullptr || alertDispatcher->idle();
    }

    void WebServer::setupStaticRoutes()
    {
        // Captive portal detection (iOS/macOS, Android, Windows, Firefox):
        // answer every probe with a redirect so the OS opens its sign-in
        // window on the WiFi setup screen.
        for (const char *probe :
             {"/hotspot-detect.html",
              "/library/test/success.html",
              "/generate_204",
              "/gen_204",
              "/success.txt",
              "/connecttest.txt",
              "/ncsi.txt",
              "/redirect",
              "/canonical.html"})
        {
            server.on(
                probe,
                HTTP_GET,
                [](AsyncWebServerRequest *request)
                {
                    if (!CaptivePortal::probeOpensSetup(arrivedViaHotspot(request)))
                    {
                        handleNotFound(request);
                        return;
                    }
                    Logger::info(TAG, "Captive check %s%s -> setup screen", request->host().c_str(), request->url().c_str());
                    request->redirect(setupScreenUrl().c_str());
                });
        }

        // Serve files from LittleFS
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache, no-store, must-revalidate");
    }

    bool WebServer::restartScheduled()
    {
        portENTER_CRITICAL(&restartPlanLock);
        const bool due = restartPlan.scheduled();
        portEXIT_CRITICAL(&restartPlanLock);
        return due;
    }

    bool WebServer::restartCancellable()
    {
        portENTER_CRITICAL(&restartPlanLock);
        const bool cancellable = restartPlan.cancellable();
        portEXIT_CRITICAL(&restartPlanLock);
        return cancellable;
    }

    bool WebServer::restartCommitted()
    {
        portENTER_CRITICAL(&restartPlanLock);
        const bool committed = restartPlan.committed();
        portEXIT_CRITICAL(&restartPlanLock);
        return committed;
    }

    void WebServer::cancelRestart()
    {
        portENTER_CRITICAL(&restartPlanLock);
        const bool cancelled = restartPlan.cancel();
        portEXIT_CRITICAL(&restartPlanLock);
        if (!cancelled || restartTimer == nullptr)
            return;
        esp_timer_stop(restartTimer);
        Logger::warn(TAG, "Scheduled restart cancelled: the roof is closing; restart it later");
    }

    bool WebServer::scheduleRestart(uint32_t delayMs, bool mandatory)
    {
        if (!restartTimer)
        {
            esp_timer_create_args_t restartTimerArgs = {};
            restartTimerArgs.callback = &restartTimerCallback;
            restartTimerArgs.arg = nullptr;
            restartTimerArgs.dispatch_method = ESP_TIMER_TASK;
            restartTimerArgs.name = "rr_restart";
            restartTimerArgs.skip_unhandled_events = false;

            if (esp_timer_create(&restartTimerArgs, &restartTimer) != ESP_OK)
            {
                Logger::error(TAG, "Failed to create restart timer");
                return false;
            }
        }

        // Recorded first: a close in between sees it (and can't cancel a mandatory one).
        portENTER_CRITICAL(&restartPlanLock);
        restartPlan.schedule(mandatory);
        portEXIT_CRITICAL(&restartPlanLock);
        esp_timer_stop(restartTimer);
        const esp_err_t started = esp_timer_start_once(restartTimer, static_cast<uint64_t>(delayMs) * 1000ULL);
        // ESP_ERR_INVALID_STATE: the callback re-armed it in between; it is
        // armed, and it runs the plan just recorded.
        if (started != ESP_OK && started != ESP_ERR_INVALID_STATE)
        {
            Logger::error(TAG, "Failed to schedule restart");
            portENTER_CRITICAL(&restartPlanLock);
            restartPlan.clear();
            portEXIT_CRITICAL(&restartPlanLock);
            return false;
        }

        return true;
    }

    bool WebServer::requireAuth(AsyncWebServerRequest *request) const
    {
        const Config &cfg = getConfigCallback();
        if (!cfg.auth.enabled || cfg.auth.password.empty())
        {
            return true;
        }
        if (!request->authenticate(cfg.auth.username.c_str(), cfg.auth.password.c_str()))
        {
            request->requestAuthentication("RoofRoof", false);
            return false;
        }
        return true;
    }

    std::string WebServer::createErrorJson(const char *error)
    {
        StaticJsonDocument<128> doc;
        doc["error"] = error;

        std::string json;
        serializeJson(doc, json);
        return json;
    }

} // namespace SQM
