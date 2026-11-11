#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "FirmwareMarker.h"
#include "HeapTrace.h"
#include "Logger.h"
#include "MQTTClient.h"
#include "MotorDriver.h"
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
        safetyInputs->updateConfig(config, millis());

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

void onArduinoOtaStart()
{
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
}

void onArduinoOtaError(ota_error_t error)
{
    static const char *const REASONS[] = {"Auth failed", "Begin failed", "Connect failed", "Receive failed", "End failed"};
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
    ArduinoOTA.onEnd([]() { Logger::info("OTA", "Update complete"); });
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
    mqttClient->begin();
    HeapTrace::mark("mqtt");

    // Spec 004: the weather verdict and park confirmation (reported only; spec 005 acts on them).
    safetyInputs = std::make_unique<SafetyInputs>(safetyPoller, mqttClient.get());
    safetyInputs->begin(config, millis());
    HeapTrace::mark("safety inputs");

    webServer = std::make_unique<WebServer>(timeManager.get(), mqttClient.get(), getConfigCallback, saveConfigCallback);
    webServer->begin();
    HeapTrace::mark("setup complete");
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

    if (arduinoOTAEnabled)
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
    if (safetyInputs)
        safetyInputs->handle(millis());

    // Small delay to prevent a tight loop.
    delay(10);
}
