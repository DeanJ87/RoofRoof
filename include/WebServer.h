#pragma once

#include "AlertDispatcher.h"
#include "AlertEngine.h"
#include "AlpacaProtocol.h"
#include "AlpacaRouter.h"
#include "Config.h"
#include "ConfigBackup.h"
#include "DeviceCore.h"
#include "HaDiscovery.h"
#include "MQTTClient.h"
#include "PlatformHold.h"
#include "RoofTypes.h"
#include "SettingsDeps.h"
#include "TimeManager.h"
#include <ArduinoJson.h>
#include <AsyncUDP.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace SQM
{
    class RoofControl;

    // The device's HTTP server: the REST API, the live-update WebSocket, the
    // web UI, Alpaca, alerts and the MQTT documents (SQMeter's WebServer
    // with the sensors, safety monitor and display removed). RoofRoof spec 002
    // onwards adds the roof.
    class WebServer
    {
    public:
        using GetConfigCallback = std::function<const Config &()>;
        using SaveConfigCallback = std::function<bool(const Config &)>;

        WebServer(TimeManager *timeMgr, MQTTClient *mqtt, GetConfigCallback getConfig, SaveConfigCallback saveConfig);
        ~WebServer();

        WebServer(const WebServer &) = delete;
        WebServer &operator=(const WebServer &) = delete;

        // The roof (spec 003); before begin(). Without it the roof routes refuse.
        void setRoof(RoofControl *control);
        // A restart is due (the roof refuses to open meanwhile); a close cancels
        // it unless it can't be cancelled (after a filesystem upload stopped
        // part-way, the filesystem is half erased: it runs once the roof stops).
        // The timer runs it only through PlatformGuard (spec 010): never while
        // the roof moves or a close waits, an ordinary one never while the
        // close sequence is busy; once committed, open and close are refused.
        static bool restartScheduled();
        static bool restartCancellable();
        static bool restartCommitted();
        static void cancelRestart();
        static bool scheduleMandatoryRestart(uint32_t delayMs) { return scheduleRestart(delayMs, true); }
        void begin();
        void handle();
        // No alert queued or being delivered.
        bool alertsIdle() const;
        // Main loop: a critical roof event from the close sequence (spec 005),
        // with its settings applied (spec 007: level never below urgent, sound,
        // wording keeping the reason), to every channel that is on, past the
        // alert pause, the send mode, the start-up grace and the master switch.
        // Returns the record id for roofAlertOutcome() (0: not stored).
        uint32_t raiseRoofAlert(const Alerts::Alert &alert);
        // Main loop, before begin(): where the alert pass reads the roof's
        // conditions for the non-critical roof events (spec 007).
        using RoofConditions = std::function<Alerts::RoofInputs()>;
        void setRoofConditions(RoofConditions source) { roofConditions = std::move(source); }
        // Any task: whether a channel took that alert.
        AlertDispatcher::Outcome roofAlertOutcome(uint32_t id) const
        {
            return alertDispatcher ? alertDispatcher->outcome(id) : AlertDispatcher::Outcome::Failed;
        }

        // Broadcast status data to /ws/status clients.
        void broadcastStatusData();

        // The setup screen is trying new WiFi credentials (POST /api/wifi/connect).
        bool isWifiConnectPending() const { return wifiConnectActive.load(); }
        // `mandatory`: a close may not cancel it (RoofIo::RestartPlan).
        static bool scheduleRestart(uint32_t delayMs, bool mandatory = false);

    private:
        static constexpr const char *TAG = "WebServer";
        static constexpr uint16_t PORT = 80;
        static constexpr uint32_t WS_STATUS_BROADCAST_INTERVAL_MS = 2000;
        static constexpr uint32_t ALERT_PASS_INTERVAL_MS = 1000;
        static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;

        AsyncWebServer server;
        AsyncWebSocket wsStatus; // /ws/status
        // Live-update clients closed since boot (SQMeter spec 011 FR-008): the
        // oldest past the per-endpoint limit, and ones that stopped reading.
        uint32_t wsReplaced = 0;
        uint32_t wsStalled = 0;
        // Live-update clients by id, with when each one's queue was first seen
        // full (0 = not full). Events arrive on the network task, the sweep
        // runs on the loop task.
        std::map<uint32_t, uint32_t> wsStatusClients;
        std::mutex wsClientsLock;
        // Imaging-app connections restored after a restart the device caused.
        bool alpacaConnectionsRestored = false;

        TimeManager *timeManager;
        MQTTClient *mqttClient;
        RoofControl *roof = nullptr;
        uint32_t lastRoofRevision = 0;
        uint32_t lastRoofBroadcast = 0;
        GetConfigCallback getConfigCallback;
        SaveConfigCallback saveConfigCallback;

        uint32_t lastStatusBroadcast = 0;
        uint32_t lastAlertPass = 0;
        uint32_t lastAlertsRevision = 0;
        std::atomic<uint32_t> configRevision{0};
        uint32_t lastConfigRevision = 0;
        // Read by the roof's command blockers (any task): a WiFi change or a
        // scan being started makes a close wait (spec 010).
        std::atomic<bool> wifiChanging{false}; // from the claim until joined or timed out
        std::atomic<bool> wifiScanStarting{false};
        std::atomic<bool> wifiConnectActive{false};
        bool wifiConnectConfigSaved = false;
        uint32_t wifiConnectStartedAt = 0;
        std::string pendingWifiSSID;
        std::string pendingWifiPassword;
        // A firmware or web UI upload is being written (the alert sender waits).
        static inline std::atomic<bool> uploadActive{false};

        AsyncUDP alpacaDiscoveryUdp; // answers in the network task, not the main loop
        // IPv6 discovery (ff12::a1:2345) needs an IPv6 address first; retried from handle().
        bool alpacaIpv6Pending = false;
        uint32_t lastAlpacaIpv6Attempt = 0;
        void retryAlpacaIpv6Discovery(uint32_t now);
        // begin(), step by step.
        void restoreAlertSchedule();
        void subscribeArmCommands();
        void sendPendingAlertTest();
        void startAlpacaDiscovery();
        static void handleNotFound(AsyncWebServerRequest *request);
        // The Alpaca HTTP API lives in lib/AlpacaLogic (Alpaca::Router) so the
        // CI simulator runs the same code; this feeds it the device's state.
        class AlpacaBackend : public Alpaca::Backend
        {
        public:
            explicit AlpacaBackend(WebServer &owner)
                : owner(owner)
            {
            }
            bool alpacaEnabled() const override;
            std::vector<Alpaca::DeviceInfo> devices() const override;
            std::string serverName() const override;
            std::string location() const override;
            std::string timestampUtc() const override;
            // The roof as the Dome (spec 006), through RoofControl with the client origin.
            Alpaca::DomeStatus domeStatus() const override;
            Alpaca::DomeAnswer domeCommand(Alpaca::DomeAction action) override;

        private:
            WebServer &owner;
        };
        static Alpaca::ServerIdentity alpacaIdentity();
        AlpacaBackend alpacaBackend{*this};
        Alpaca::Router alpacaRouter{alpacaBackend, alpacaIdentity()};
        TaskHandle_t loopTaskHandle = nullptr;

        // Alerts
        Alerts::AlertEngine alertEngine;
        RoofConditions roofConditions;
        bool roofAnnouncedRestored = false;
        void keepRoofAnnounced();

        // When alerts go out (SQMeter spec 021): the send mode and the pause
        // state ("armed"), with why and since when. Saved in NVS. Pause/resume
        // requests come from HTTP (AsyncTCP task) and MQTT; the loop task
        // applies them, and Alpaca connects in the alert pass.
        Alerts::AlertSchedule alertSchedule;
        // -1 none, else source * 2 + (resume ? 1 : 0), source an Alerts::ScheduleReason.
        std::atomic<int8_t> pendingArm{-1};
        static int8_t encodeArm(bool resume, Alerts::ScheduleReason source)
        {
            return static_cast<int8_t>(static_cast<int>(source) * 2 + (resume ? 1 : 0));
        }
        // Whether the imaging app is still checking each Alpaca device.
        Alpaca::ClientWatch clientWatch;
        // Copies for the HTTP handlers, which run on the AsyncTCP task.
        Alerts::ScheduleState scheduleShared;
        Alpaca::ClientWatch clientWatchShared;
        mutable portMUX_TYPE scheduleLock = portMUX_INITIALIZER_UNLOCKED;
        void shareSchedule();
        Alerts::ScheduleState sharedSchedule() const; // with any pending pause/resume applied
        Alpaca::ClientWatch sharedClientWatch() const;
        uint32_t mqttArmedConnection = 0xFFFFFFFF;
        void applyPendingArm();
        void scheduleChanged(bool wasSending);
        void publishArmedState();
        std::unique_ptr<AlertDispatcher> alertDispatcher;
        // Set by the HTTP handler, sent from the loop task.
        struct PendingAlertTest
        {
            uint8_t mask = 0;
            const Core::SampleAlert *sample = nullptr; // nullptr: the generic test
            uint8_t level = 2;
            char sound[33] = {};
            char title[AlertsConfig::MAX_TEMPLATE_TITLE + 1] = {};
            char message[AlertsConfig::MAX_TEMPLATE_MESSAGE + 1] = {};
        };
        PendingAlertTest pendingAlertTest;
        portMUX_TYPE pendingAlertTestLock = portMUX_INITIALIZER_UNLOCKED;
        std::string discoveryKey;
        uint32_t discoveryConnection = 0xFFFFFFFF;
        HaDiscovery::Device discoveryDevice;
        bool discoveryWasOn = false;
        void publishDiscovery();
        void processAlerts();
        void watchClients(Alerts::AlertInputs &inputs, const Config &cfg, const std::string &localTime);
        void sendAlerts(const Core::AlertStep &step, const Config &cfg);

        // What the settings dependencies need to know (lib/SettingsDeps).
        Deps::Facts settingsFacts() const;
        // Alert channels switched on but inactive, with the reason. Alerts
        // being off doesn't block: tests work while alerts are off.
        ChannelBlocks channelBlocks() const;
        // "HH:MM" and "YYYY-MM-DD" in the device's time zone, or "--:--"/"--" before the clock is set.
        static void localClock(std::string &timeText, std::string &dateText);
        void setupAlertRoutes();
        void handleAlertTest(AsyncWebServerRequest *request);
        void handleAlertsClear(AsyncWebServerRequest *request);
        void handleArm(AsyncWebServerRequest *request, bool armed);
        void handleAlertsArmed(AsyncWebServerRequest *request);
        void handleAlertsRecent(AsyncWebServerRequest *request);

        // Setup route handlers
        void setupStaticRoutes();
        void setupAPIRoutes();
        void setupWebSocket();
        void setupOTA();
        void handleFsUploadDone(AsyncWebServerRequest *request);
        void handleFirmwareUploadDone(AsyncWebServerRequest *request);
        void setupAlpacaRoutes();
        void handleAlpacaRequest(AsyncWebServerRequest *request);

        // API endpoint handlers
        void handleGetStatus(AsyncWebServerRequest *request);
        void handleGetConfig(AsyncWebServerRequest *request);
        void handleRestart(AsyncWebServerRequest *request);
        void handleWiFiScan(AsyncWebServerRequest *request);
        void handleMQTTTest(AsyncWebServerRequest *request, JsonVariant &json);
        void pollWiFiConnect();
        // setupAPIRoutes(), by group (registration order matters: more
        // specific paths first).
        void setupSettingsRoutes();
        void setupNetworkRoutes();
        void handleSettingsEffective(AsyncWebServerRequest *request);
        void handleSetConfig(AsyncWebServerRequest *request, JsonVariant &json);
        // Saves settings and applies what takes effect at once (a settings save and a restore).
        bool saveAndApply(const Config &next);
        // saveAndApply() counted as a flash write (spec 010); answers 409 when
        // it must wait for the roof (or, with `restarts`, while a restart would
        // be held) and 500 when the save failed. True when saved.
        bool saveAsFlashWrite(AsyncWebServerRequest *request, const Config &next, bool restarts = false);

        // The roof (src/WebServerRoof.cpp).
        void setupRoofRoutes();
        void handleRoofCommand(AsyncWebServerRequest *request, Roof::Command command);
        void handleRoofClearFault(AsyncWebServerRequest *request);
        // Answers 409 with the reason and returns true while `action` must wait
        // (spec 010, RoofIo::platformHold): the roof moves, a close waits, or
        // for restarts, firmware and Wi-Fi changes the close sequence is busy.
        bool refuseIfHeld(AsyncWebServerRequest *request, RoofIo::PlatformAction action);
        // Roof commands need the X-RoofRoof: 1 header (answers 403 without it).
        bool requireCommandHeader(AsyncWebServerRequest *request);
        bool roofMoving() const;
        // Uploads and the roof (src/WebServerUpdates.cpp): per-request state in _tempObject.
        bool uploadMayWrite(AsyncWebServerRequest *request, size_t index, bool firmware);
        bool answerStoppedUpload(AsyncWebServerRequest *request);
        // The alerts pause state waits for the roof to stop before it is saved.
        bool armedSavePending = false;
        void saveArmedState();
        void appendRoof(JsonDocument &doc) const;

        // Settings backup and restore (SQMeter spec 033, src/WebServerBackup.cpp).
        struct BackupJob;
        struct StagedRestore
        {
            explicit StagedRestore(const Config &current)
                : preview(current)
            {
            }
            std::string token;
            Backup::Preview preview; // preview.candidate is what apply saves
            uint32_t revision = 0;
            uint32_t stagedAtMs = 0;
        };
        static inline std::atomic<bool> backupJobRunning{false};
        std::unique_ptr<StagedRestore> stagedRestore; // the previewed restore, until applied
        std::mutex stagedRestoreLock;
        void setupBackupRoutes();
        void handleBackupStart(AsyncWebServerRequest *request, bool preview);
        void handleBackupApply(AsyncWebServerRequest *request);
        static void runBackupJob(void *arg);
        static std::string makeBackupFile(BackupJob &job, int &status);
        std::string previewRestore(BackupJob &job, int &status);
        static Backup::DeviceInfo backupDevice();
        void handleWiFiConnect(AsyncWebServerRequest *request, JsonVariant &json);

        // WebSocket handlers
        void prepareWebSocketClient(std::map<uint32_t, uint32_t> &clients, AsyncWebSocketClient *client);
        void capWebSocketClients(AsyncWebSocket &socket);
        void noteWebSocketClosed(std::map<uint32_t, uint32_t> &clients, AsyncWebSocketClient *client);
        void closeStalledClients(AsyncWebSocket &socket, std::map<uint32_t, uint32_t> &clients, uint32_t now);
        void appendConnections(JsonDocument &doc) const;
        void restoreAlpacaConnections();
        void rememberAlpacaConnections();
        void onStatusWebSocketEvent(AsyncWebSocketClient *client, AwsEventType type);

        // Helper functions
        bool requireAuth(AsyncWebServerRequest *request) const;
        // The request carries the password when protection is on (no reply sent).
        bool requestAuthorised(AsyncWebServerRequest *request) const;
        std::string createStatusJson() const;
        // createStatusJson() sections, in document order.
        void appendFirmware(JsonDocument &doc) const;
        void appendRuntime(JsonDocument &doc) const;
        void appendTime(JsonDocument &doc) const;
        void appendWifi(JsonDocument &doc) const;
        void appendMqttStatus(JsonDocument &doc) const;
        static std::string createErrorJson(const char *error);
    };

} // namespace SQM
