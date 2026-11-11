#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "FirmwareMarker.h"
#include "FlashGuard.h"
#include "HeapTrace.h"
#include "Logger.h"
#include "MQTTClient.h"
#include "MotionGuards.h"
#include "MotorDriver.h"
#include "RoofControl.h"
#include "SafetyInputs.h"
#include "SafetyPoller.h"
#include "TimeManager.h"
#include "WebServer.h"
#include "WiFiManager.h"

using namespace SQM;

// Global objects
static Config config;
static std::unique_ptr<WiFiManager> wifiManager;
static std::unique_ptr<TimeManager> timeManager;
static std::unique_ptr<WebServer> webServer;
static std::unique_ptr<MQTTClient> mqttClient;
static SafetyPoller safetyPoller;
static std::unique_ptr<SafetyInputs> safetyInputs;
static RoofControl roofControl;
static bool arduinoOTAEnabled = false;
RTC_DATA_ATTR uint32_t bootCount = 0;

// The default 8 KB left only ~1.5 KB spare at peak on SQMeter (alert
// processing, config saves). 4 KB more is cheap insurance against a
// stack-overflow reboot.
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

// Callbacks for WebServer
const Config &getConfigCallback()
{
    return config;
}

bool saveConfigCallback(const Config &newConfig)
{
    config = newConfig;
    const bool saved = config.save();
    if (!saved)
        return false;

    if (mqttClient)
        mqttClient->updateConfig(config.mqtt);
    if (timeManager)
        timeManager->updateConfig(config.ntp);
    if (safetyInputs)
        safetyInputs->updateConfig(config);
    roofControl.updateConfig(config); // taken by the roof once no move is active

    if (!config.ota.enabled || config.ota.password.empty())
        arduinoOTAEnabled = false;
    else if (!arduinoOTAEnabled)
        Logger::warn("Main", "ArduinoOTA config saved; restart required to start command-line OTA");
    return true;
}

// SQMeter spec 027 FR-021: tell the Arduino core not to mark a new firmware
// valid at start-up; loop() does it once the device is up.
extern "C" bool verifyRollbackLater()
{
    return true;
}

void setupWatchdog()
{
    Logger::info("Main", "Configuring watchdog timer (30s)");
    // IDF 5: the task watchdog is already initialised by the core; reconfigure it.
    const esp_task_wdt_config_t wdtConfig = {.timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true};
    esp_task_wdt_reconfigure(&wdtConfig);
    esp_task_wdt_add(NULL);
}

void loadConfig()
{
    if (Config::load(config))
    {
        Logger::info("Main", "Configuration loaded");
    }
    else
    {
        config = Config::createDefault();
        Logger::warn("Main", "Using default configuration");
    }
    HeapTrace::mark("config loaded");
}

void mountFilesystem()
{
    if (!LittleFS.begin(false))
        Logger::error("Main", "Failed to mount LittleFS; filesystem not formatted automatically");
    else
        Logger::info("Main", "LittleFS mounted successfully");
}

// Join the saved network, or open the setup hotspot (captive portal).
void startWifi()
{
    // The network is RoofRoof's own setting (config.wifi): the WiFi driver
    // keeps no copy in NVS, so a reconnect while the roof moves writes no flash.
    WiFi.persistent(false);
    wifiManager = std::make_unique<WiFiManager>(config.wifi);
    wifiManager->begin();

    // Give the saved network a moment so most boots start fully connected. If
    // it isn't joined yet, start anyway: WiFiManager keeps trying, and opens
    // the setup hotspot only if it still hasn't joined after
    // CaptivePortal::FALLBACK_AFTER_MS (SQMeter spec 014). A saved network that
    // isn't in range ends the wait at once.
    int attempts = 0;
    while (!wifiManager->isConnected() && !wifiManager->isInAPMode() && attempts < 20)
    {
        if (WiFi.status() == WL_NO_SSID_AVAIL)
        {
            Logger::warn("Main", "Saved network not in range");
            break;
        }
        delay(500);
        attempts++;
    }
    if (!wifiManager->isConnected() && !wifiManager->isInAPMode())
        Logger::warn("Main", "WiFi not joined yet; still trying");
    wifiManager->startMdns();
    HeapTrace::mark("wifi");
}

// Command-line OTA writes the flash: the roof refuses to open meanwhile (spec 003).
static uint32_t arduinoOtaToken = 0;

void onArduinoOtaStart()
{
    // The roof may have moved since handle() was let through: refuse unless it
    // is still closed. Aborted before the first write; ArduinoOTA then reports an error.
    if (!RoofIo::commandLineOtaAllowed(roofControl.state()))
    {
        arduinoOtaToken = 0;
        Update.abort();
        Logger::warn("OTA", "Command-line OTA refused: the roof isn't closed");
        return;
    }
    arduinoOtaToken = FlashGuard::begin();
    const bool firmware = ArduinoOTA.getCommand() == U_FLASH;
    if (!firmware)
        LittleFS.end(); // writing the filesystem: unmount it first
    Logger::info("OTA", "Start updating %s", firmware ? "firmware" : "filesystem");
}

void onArduinoOtaProgress(unsigned int progress, unsigned int total)
{
    static int lastPercent = -1;
    const int percent = static_cast<int>(progress / (total / 100));
    if (percent != lastPercent && percent % 10 == 0)
    {
        Logger::info("OTA", "Progress: %u%%", percent);
        lastPercent = percent;
    }
    FlashGuard::progress(arduinoOtaToken);
}

void onArduinoOtaError(ota_error_t error)
{
    static const char *const REASONS[] = {"Auth failed", "Begin failed", "Connect failed", "Receive failed", "End failed"};
    FlashGuard::end(arduinoOtaToken);
    const unsigned index = static_cast<unsigned>(error);
    Logger::error("OTA", "Error[%u]: %s", index, index < 5 ? REASONS[index] : "unknown");
}

// ArduinoOTA for command-line firmware uploads, only when configured securely.
void startArduinoOta()
{
    if (!wifiManager->isConnected())
        return;
    if (!config.ota.enabled || config.ota.password.empty())
    {
        Logger::warn("Main", "ArduinoOTA disabled; configure ota.enabled and ota.password to enable command-line OTA");
        return;
    }
    ArduinoOTA.setHostname(config.wifi.hostname.c_str());
    ArduinoOTA.setPassword(config.ota.password.c_str());
    ArduinoOTA.setMdnsEnabled(false); // WiFiManager owns mDNS (and it can be off)
    ArduinoOTA.onStart(onArduinoOtaStart);
    ArduinoOTA.onEnd(
        []()
        {
            FlashGuard::end(arduinoOtaToken);
            Logger::info("OTA", "Update complete");
        });
    ArduinoOTA.onProgress(onArduinoOtaProgress);
    ArduinoOTA.onError(onArduinoOtaError);
    ArduinoOTA.begin();
    if (wifiManager->isMdnsRunning())
        MDNS.enableArduino(3232, true);
    arduinoOTAEnabled = true;
    Logger::info("Main", "ArduinoOTA enabled with password authentication");
}

// Time, MQTT, then the web server, which needs them both.
void startServices()
{
    timeManager = std::make_unique<TimeManager>(config.ntp);
    if (wifiManager->isConnected())
        timeManager->begin();
    HeapTrace::mark("ota + time");

    mqttClient = std::make_unique<MQTTClient>(config.mqtt);
    // Spec 003: no MQTT connect while the roof moves or a close waits.
    mqttClient->setConnectHold([]() { return roofControl.moving() || roofControl.closeWaiting(); });
    mqttClient->begin();
    HeapTrace::mark("mqtt");

    // Spec 004: the weather verdict and park confirmation (reported only; spec 005 acts on them).
    safetyInputs = std::make_unique<SafetyInputs>(safetyPoller, mqttClient.get());
    safetyInputs->begin(config);
    HeapTrace::mark("safety inputs");

    webServer = std::make_unique<WebServer>(timeManager.get(), mqttClient.get(), getConfigCallback, saveConfigCallback);
    webServer->setRoof(&roofControl);
    webServer->begin();
    HeapTrace::mark("setup complete");
}

// The roof's switch readings into the safety inputs (spec 004), and their
// verdict and park result back to the roof's gates (spec 003), every 250 ms.
void handleSafety()
{
    if (!safetyInputs)
        return;
    safetyInputs->setParkSwitches(roofControl.parkSwitch(0), roofControl.parkSwitch(1));
    safetyInputs->setRainContact(roofControl.rainContact());
    safetyInputs->handle();
    static uint32_t lastHandOver = 0;
    const uint32_t now = millis();
    if (now - lastHandOver >= 250)
    {
        roofControl.setSafety(safetyInputs->weather(), safetyInputs->park());
        lastHandOver = now;
    }
}

void setup()
{
    // Before anything else: the motor driver off (constitution Principle I).
    MotorDriver::holdOff();

    Serial.begin(115200);
    delay(100);
    bootCount++;

    Logger::init();
    Logger::info("Main", "=== RoofRoof Starting ===");
    Logger::info("Main", "ESP32 Chip: %s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
    Logger::info("Main", "Flash: %d bytes", ESP.getFlashChipSize());
    // The image's own marker and the partition layout it runs on.
    Logger::info("Main", "Firmware %s partitions %s", FirmwareMarker::fields(), FirmwareMarker::layout().c_str());
    HeapTrace::mark("boot");

    setupWatchdog();
    loadConfig();
    // Spec 003: the roof's control task, independent of WiFi and the web
    // server. Any fault saved before the restart is latched before its first
    // step; between the magnets the position is unknown and nothing moves.
    roofControl.begin(config);
    HeapTrace::mark("roof");
    mountFilesystem();
    startWifi();
    startArduinoOta();
    startServices();

#ifdef RR_TEST_BOOT_CRASH
    // Rollback test build only (SQMeter spec 027 device check): a firmware that
    // dies while starting must fall back to the previous one.
    abort();
#endif
    Logger::info("Main", "=== Setup complete ===");
    Logger::info("Main", "IP Address: %s", wifiManager->getIPAddress().c_str());
    Logger::info("Main", "MAC Address: %s", wifiManager->getMACAddress().c_str());
}

void loop()
{
    esp_task_wdt_reset();

    // While the roof opens or closes (spec 003): no restart, no command-line
    // OTA (flash writes stall its control task and watchdog) and no MQTT
    // reconnect (it could hold this loop past the safety hand-over limit).
    WebServer::holdRestarts(roofControl.moving() || roofControl.closeWaiting());

    wifiManager->setRetryPaused(webServer->isWifiConnectPending());
    wifiManager->handle();

    // Joined a network from the setup hotspot: restart so time, MQTT, Alpaca
    // discovery and OTA start normally. The delay lets the setup screen show
    // the new address first.
    static bool restartingFromHotspot = false;
    if (!restartingFromHotspot && wifiManager->connectedFromHotspotFor(15000))
    {
        Logger::info("Main", "Connected to WiFi from the setup hotspot; restarting");
        restartingFromHotspot = WebServer::scheduleRestart(1000);
    }

    // Command-line OTA blocks this loop for the whole upload and can't be
    // stopped part-way, so it is only serviced while the roof is closed: a
    // close can never be waiting behind it (spec 003 FR-030).
    if (arduinoOTAEnabled && RoofIo::commandLineOtaAllowed(roofControl.state()))
        ArduinoOTA.handle();
    if (timeManager)
        timeManager->handle();
    webServer->handle();

    // SQMeter spec 027 FR-021: a new firmware is only kept once it has
    // started properly; one that fails before this point rolls back on its
    // next boot.
    static bool firmwareConfirmed = false;
    if (!firmwareConfirmed)
    {
        esp_ota_mark_app_valid_cancel_rollback();
        firmwareConfirmed = true;
    }

    if (mqttClient)
        mqttClient->handle();
    handleSafety();

    // Small delay to prevent a tight loop.
    delay(10);
}
