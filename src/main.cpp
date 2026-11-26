#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <memory>
#include <mutex>

#include "CloseOnUnsafe.h"
#include "CrashLog.h"
#include "Diagnostics.h"
#include "EthernetLink.h"
#include "NetState.h"
#include "PinSetup.h"
#include "RoofText.h"
#include "Config.h"
#include "FirmwareMarker.h"
#include "FlashGuard.h"
#include "HeapMap.h"
#include "HeapReserve.h"
#include <esp_heap_caps.h>
#include "HeapTrace.h"
#include "Logger.h"
#include "MQTTClient.h"
#include "MotionGuards.h"
#include "MotorDriver.h"
#include "OtaImageCheck.h"
#include "PlatformGuard.h"
#include "ResetReason.h"
#include "RoofControl.h"
#include "SafetyInputs.h"
#include "SafetyPoller.h"
#include "SqmeterSite.h"
#include "SiteSync.h"
#include "SqmeterSync.h"
#include "TimeManager.h"
#include "WebServer.h"
#include "WiFiManager.h"
#include "WiringRun.h"

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
static CloseOnUnsafe closeOnUnsafe;
static Diagnostics diagnostics; // spec 017: off unless diag.enabled
static bool arduinoOTAEnabled = false;
RTC_DATA_ATTR uint32_t bootCount = 0;
bool roofBusy(); // spec 020, below
// Spec 020: one settings save at a time (the web server's saves on the async
// TCP task, the loop's SQMeter saves), so neither can undo the other's.
static std::mutex configSaveLock;
void siteSaved(bool settingsUnchanged); // spec 020, below
static SiteSync siteSync(config, configSaveLock, safetyPoller, siteSaved);

// The default 8 KB left only ~1.5 KB spare at peak on SQMeter (alert
// processing, config saves). Spec 038: RoofRoof's loop peaked at about 7.3 KB
// over a day of bench and shakedown runs (crash snapshots); 10 KB keeps about
// 2.9 KB spare and gives 2 KB back to the heap (was 12 KB).
SET_LOOP_TASK_STACK_SIZE(10 * 1024);

// Callbacks for WebServer
const Config &getConfigCallback()
{
    return config;
}

// Spec 020: whether any settings group follows SQMeter, and where it is.
void tellPollerTheSite()
{
    const SqmeterSyncConfig &s = config.sqmeterSync;
    safetyPoller.setSite(SafetyPoller::SiteTarget{
        config.safety.httpUrl,
        config.safety.httpUsername,
        config.safety.httpPassword,
        config.location.source == LocationConfig::Source::Sqmeter,
        s.time.follow || s.mqtt.follow || s.alerts.follow,
        s.mqtt.follow || s.alerts.follow});
}

// What a saved change applies without a restart. Under configSaveLock.
void applySavedConfig()
{
    if (mqttClient)
        mqttClient->updateConfig(config.mqtt);
    if (timeManager)
        timeManager->updateConfig(config.ntp);
    if (safetyInputs)
        safetyInputs->updateConfig(config);
    tellPollerTheSite();              // spec 020
    roofControl.updateConfig(config); // taken by the roof once no move is active
    closeOnUnsafe.updateConfig(config);
    diagnostics.updateConfig(config.diag); // applied by the main loop

    if (!config.ota.enabled || config.ota.password.empty())
        arduinoOTAEnabled = false;
    else if (!arduinoOTAEnabled)
        Logger::warn("Main", "ArduinoOTA config saved; restart required to start command-line OTA");
}

// Every web save: `edit` builds the settings from the live ones under the save
// lock (spec 021: the stale check and the merge see what is saved), and the
// revision is counted before the lock is let go, so the next save sees it.
bool updateConfigCallback(const WebServer::EditConfig &edit)
{
    const std::lock_guard<std::mutex> held(configSaveLock);
    // On the heap: several ~2 KB settings copies add up on a stack (spec 022;
    // fix 041: the web server runs this on its save task, SaveTask).
    const std::unique_ptr<Config> next(new Config(config));
    if (!edit(config, *next))
        return false;
    // Spec 020 FR-008, FR-028: against the live settings, a copy taken before
    // the last location save or sync can't put older values back.
    Site::keepFollowed(config, *next);
    config = *next;
    const bool saved = config.save();
    if (!saved)
        return false;
    applySavedConfig();
    if (webServer)
        webServer->noteConfigSaved();
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

// Spec 023: `storedRejected` when settings were stored but couldn't be read.
void loadConfig(bool &storedRejected)
{
    if (Config::load(config, &storedRejected))
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

// Join the saved network, or open the setup hotspot (captive portal). Spec
// 023: on the WT32-ETH01 Ethernet first, Wi-Fi as the plan says.
void startWifi()
{
    EthernetLink::begin(config);
    // The network is RoofRoof's own setting (config.wifi): the WiFi driver
    // keeps no copy in NVS, so a reconnect while the roof moves writes no flash.
    WiFi.persistent(false);
    const Net::NetMode mode = !EthernetLink::present()                               ? Net::NetMode::Wifi
                              : config.network.mode == NetworkConfig::Mode::Ethernet ? Net::NetMode::Ethernet
                                                                                     : Net::NetMode::Auto;
    NetState::setEthernetOnly(mode == Net::NetMode::Ethernet);
    wifiManager = std::make_unique<WiFiManager>(config.wifi, mode);
    wifiManager->begin();
    if (EthernetLink::present())
    {
        // Most boots have an address over the cable within a few seconds.
        for (int waited = 0; waited < 50 && !EthernetLink::hasAddress(); ++waited)
            delay(100);
        if (!EthernetLink::hasAddress())
            Logger::warn("Main", "No Ethernet address yet; still trying");
        wifiManager->startMdns();
        HeapTrace::mark("network");
        return;
    }

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
static bool arduinoOtaFirmware = true;

void onArduinoOtaStart()
{
    // The roof may have moved since handle() was let through: refuse unless it
    // is still closed. Aborted before the first write; ArduinoOTA then reports an error.
    // Checked and begun under the roof's lock (spec 010): the roof closed, no
    // close waiting and, for firmware, the close sequence idle.
    const bool firmware = ArduinoOTA.getCommand() == U_FLASH;
    arduinoOtaFirmware = firmware;
    RoofIo::HoldReason why = RoofIo::HoldReason::None;
    arduinoOtaToken =
        PlatformGuard::beginUpload(firmware ? RoofIo::PlatformAction::FirmwareUpload : RoofIo::PlatformAction::FilesystemUpload, why, true);
    if (arduinoOtaToken == 0)
    {
        Update.abort();
        Logger::warn(
            "OTA", "Command-line OTA refused: %s", why == RoofIo::HoldReason::Moving ? "the roof isn't closed" : RoofIo::holdText(why));
        return;
    }
    if (!firmware)
        LittleFS.end(); // writing the filesystem: unmount it first
    else
        OtaImageCheck::beginScan(); // spec 011: the marker is checked as the image arrives
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
    // Stamped under the roof's lock after each chunk (spec 010; ArduinoOTA
    // calls this once the chunk is written). If the roof moved or a close
    // waits after a stall, or the activity was stopped, nothing more is
    // written. A filesystem stopped part-way was unmounted: it must restart.
    // Spec 011: an image this board refuses (a bench image on a standard
    // board, another product or layout) is stopped as soon as its marker is
    // read, so the boot partition is never switched and the tool reports it.
    std::string refused;
    if (arduinoOtaToken != 0 && arduinoOtaFirmware && OtaImageCheck::scanWritten(progress, refused))
    {
        Logger::error("OTA", "Command-line firmware refused: %s", refused.c_str());
        Update.abort();
        FlashGuard::end(arduinoOtaToken);
        arduinoOtaToken = 0;
        return;
    }
    if (arduinoOtaToken != 0 &&
        (PlatformGuard::keepUpload(arduinoOtaToken) != RoofIo::HoldReason::None || FlashGuard::shouldStop(arduinoOtaToken)))
    {
        Logger::warn("OTA", "Command-line OTA stopped part-way: the roof needs the flash");
        Update.abort();
        if (!arduinoOtaFirmware)
            WebServer::scheduleMandatoryRestart(1000);
        FlashGuard::end(arduinoOtaToken);
        arduinoOtaToken = 0;
    }
}

void onArduinoOtaError(ota_error_t error)
{
    static const char *const REASONS[] = {"Auth failed", "Begin failed", "Connect failed", "Receive failed", "End failed"};
    FlashGuard::end(arduinoOtaToken);
    const unsigned index = static_cast<unsigned>(error);
    Logger::error("OTA", "Error[%u]: %s", index, index < 5 ? REASONS[index] : "unknown");
}

// ArduinoOTA for command-line firmware uploads, only when configured securely.
// Spec 023: once, when the device is first on a network (Wi-Fi or Ethernet).
void startArduinoOta()
{
    if (!config.ota.enabled || config.ota.password.empty())
    {
        Logger::warn("Main", "ArduinoOTA disabled; configure ota.enabled and ota.password to enable command-line OTA");
        return;
    }
    ArduinoOTA.setHostname(config.wifi.hostname.c_str());
    ArduinoOTA.setPassword(config.ota.password.c_str());
    ArduinoOTA.setMdnsEnabled(false); // WiFiManager owns mDNS (and it can be off)
    ArduinoOTA.onStart(onArduinoOtaStart);
    // The restart goes through the restart plan like every other (spec 010):
    // scheduled before the flash activity ends, so no open starts in between,
    // and it waits for an unsaved fault, a short write and (firmware) the
    // close sequence. A web UI image must be remounted: mandatory.
    ArduinoOTA.setRebootOnSuccess(false);
    ArduinoOTA.onEnd(
        []()
        {
            // Spec 011: the web upload's marker check, after the fact (a bench
            // image never boots on a standard board).
            std::string why;
            if (arduinoOtaFirmware && OtaImageCheck::revertIfRefused(why))
            {
                Logger::error("OTA", "Command-line firmware refused, the running firmware stays: %s", why.c_str());
                FlashGuard::end(arduinoOtaToken);
                return;
            }
            Logger::info("OTA", "Update complete; restarting");
            WebServer::scheduleRestart(1000, !arduinoOtaFirmware);
            FlashGuard::end(arduinoOtaToken);
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
    // Spec 023: begun once on a network (Wi-Fi or Ethernet); see startWhenOnline().
    HeapTrace::mark("ota + time");

    mqttClient = std::make_unique<MQTTClient>(config.mqtt);
    // Spec 003: no MQTT connect while the roof moves or a close waits. Asked
    // under the roof's lock after the connect is marked in flight (spec 010),
    // so a command decided at the same moment is seen by one side or the other.
    mqttClient->setConnectHold([]() { return roofControl.whileStill({}) != RoofIo::HoldReason::None; });
    mqttClient->begin();
    HeapTrace::mark("mqtt");

    // Spec 004: the weather verdict and park confirmation.
    safetyInputs = std::make_unique<SafetyInputs>(safetyPoller, mqttClient.get());
    tellPollerTheSite(); // spec 020
    safetyInputs->begin(config);
    HeapTrace::mark("safety inputs");

    webServer = std::make_unique<WebServer>(timeManager.get(), mqttClient.get(), getConfigCallback, updateConfigCallback);
    webServer->setRoof(&roofControl);
    webServer->setDiagnostics(&diagnostics);
    // Spec 008: the automatic close's phase in the status and the MQTT document.
    webServer->setClosePhase([]() { return CloseSeq::phaseName(closeOnUnsafe.phase()); });
    webServer->setCloseHold([]() { return closeOnUnsafe.standDown(); }, []() { return closeOnUnsafe.requestRelease(); });
    webServer->setWeatherHold([]() { return closeOnUnsafe.weatherHoldStatus(); }); // spec 030
    webServer->setDaylight([]() { return closeOnUnsafe.daylightStatus(); });
    // Spec 015: the wizard's Test reads the inputs as the main loop last saw them.
    webServer->setInputs([]() { return safetyInputs->report(); });
    // Spec 020: the location's source and age, and the copy refused while busy.
    webServer->setSiteStatus([](JsonDocument &doc) { siteSync.writeStatus(doc); });
    webServer->setRoofBusy(roofBusy);
    // Spec 023: the wiring check, run on this loop; busy as for a site copy.
    WiringRun::begin(&roofControl, roofBusy, []() { return config.pins != PinSetup::running(); });
    webServer->begin();

    // Spec 005: unsafe ends parked and closed; its critical events go out
    // through the alert dispatcher, never through N.I.N.A. or SQMeter. Its
    // record was restored in setup(), before the web server could ask.
    // Spec 030: through the alert outbox, kept until a channel takes them.
    closeOnUnsafe.setAlertOut(
        {[](const Alerts::Alert &alert, bool resolvable) { webServer->queueRoofAlert(alert, resolvable); },
         [](Alerts::AlertType type) { webServer->resolveRoofAlert(type); },
         [](uint64_t ms) { webServer->setAlertMaxGap(ms); }});
    webServer->setNeedsPerson([]() { return closeOnUnsafe.needsPersonStatus(); });
    // Spec 007: the roof's non-critical events, read once a second by the alert pass.
    webServer->setRoofConditions([]() { return closeOnUnsafe.conditions(); });
    HeapTrace::mark("setup complete");
}

// Spec 020: no SQMeter read, copy or save while the roof is busy (spec 010).
bool roofBusy()
{
    return roofControl.moving() || roofControl.closeWaiting() || closeOnUnsafe.platformState() == RoofIo::SequenceState::Working;
}

// Spec 020: a save of what was read from SQMeter. Only the location or a
// group's syncedAt: daylight takes it; a synced group: applied as any save.
void siteSaved(bool settingsUnchanged)
{
    if (settingsUnchanged)
        closeOnUnsafe.updateConfig(config);
    else
        applySavedConfig();
    if (webServer)
        webServer->noteConfigSaved();
}

// The roof's switch readings into the safety inputs (spec 004), their verdict
// and park result back to the roof's gates (spec 003) every 250 ms (at once when
// the verdict stops being safe), and the
// close on unsafe weather (spec 005) every pass.
void handleSafety()
{
    if (!safetyInputs)
        return;
    safetyInputs->setParkSwitches(roofControl.parkSwitch(0), roofControl.parkSwitch(1));
    safetyInputs->handle();
    static uint32_t lastHandOver = 0;
    static bool handedSafe = false;
    const uint32_t now = millis();
    // A verdict that stops being safe is handed over in the same pass the
    // close sequence first sees it, so the roof's checks (an Alpaca client's
    // stop of a close, spec 006) never lag the sequence.
    // Spec 013: daylight is handed over with them, at once when it starts
    // wanting the roof closed.
    closeOnUnsafe.updateDaylight(safetyInputs->testBox().daylight);
    static bool handedCloses = false;
    const bool safeNow = safetyInputs->weather().safe();
    const bool closesNow = closeOnUnsafe.daylight().closes();
    if (now - lastHandOver >= 250 || (handedSafe && !safeNow) || (closesNow && !handedCloses))
    {
        roofControl.setSafety(
            safetyInputs->weather(),
            safetyInputs->park(),
            safetyInputs->closeHold(),
            closesNow,
            closesNow ? Daylight::describe(closeOnUnsafe.daylight()) : "");
        lastHandOver = now;
        handedSafe = safeNow;
        handedCloses = closesNow;
    }
    closeOnUnsafe.handle(*safetyInputs, roofControl);
    // Spec 027: a command waiting for the park asks N.I.N.A. (spec 043: only at an end); a wait that gave up alerts.
    if (roofControl.takeParkRequest() && safetyInputs->ninaSet() && roofControl.parkRequestAllowed())
    {
        Logger::info("Roof", "Asking N.I.N.A. to park the mount before the roof moves");
        safetyInputs->requestPark();
    }
    std::string gaveUp;
    if (roofControl.takeWaitAlert(gaveUp))
        closeOnUnsafe.parkWaitFailed(gaveUp);
    // Spec 039: a person set the park check aside: an information alert.
    std::string overridden;
    if (roofControl.takeOverrideAlert(overridden))
        closeOnUnsafe.parkOverridden(overridden);
    // Spec 047: a move finished without a fresh park reading: an information alert.
    std::string staleFinish;
    if (roofControl.takeStaleAlert(staleFinish))
        closeOnUnsafe.finishedOnStale(staleFinish);
}

// Spec 023: NTP and command-line OTA start the first time the device is on a
// network, whichever it is and whenever it happens.
void startWhenOnline()
{
    static bool started = false;
    if (started || !NetState::online())
        return;
    started = true;
    if (timeManager)
        timeManager->begin();
    startArduinoOta();
}

void setup()
{
    // Constitution I (4.0.0): the pins are settings (spec 023), so the
    // settings are read first; until EN is driven low the EN pull-down holds
    // the driver off, as it does through reset and the boot loader. Serial
    // and the log use UART0 only.
    Serial.begin(115200);
    CrashLog::arm(); // spec 029: before the first log line
    Logger::init();
    // Spec 038: the emergency reserve, taken while the heap is whole.
    HeapSafe::armReserve();
    bool storedRejected = false;
    loadConfig(storedRejected);
    PinSetup::begin(config, storedRejected); // EN, then RPWM and LPWM low; or nothing driven
    // Spec 029 FR-009: EN is low and the roof's task hasn't started, so the
    // one flash write of a crash record can't wait behind a move.
    CrashLog::keepRecord();
    delay(100);
    bootCount++;

    Logger::info("Main", "=== RoofRoof Starting ===");
    // A reset mid-move (brown-out, crash, watchdog) leaves the position unknown
    // between the magnets; the reason helps tell which (spec 010 FR-015).
    Logger::info("Main", "Reset reason: %s", resetReasonText(esp_reset_reason()));
    Logger::info("Main", "ESP32 Chip: %s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
    Logger::info("Main", "Flash: %d bytes", ESP.getFlashChipSize());
    // The image's own marker and the partition layout it runs on.
    Logger::info("Main", "Firmware %s partitions %s", FirmwareMarker::fields(), FirmwareMarker::layout().c_str());
    HeapTrace::mark("boot");

    setupWatchdog();
    // Spec 003: the roof's control task, independent of WiFi and the web
    // server. Any fault saved before the restart is latched before its first
    // step; between the magnets the position is unknown and nothing moves.
    // Spec 023: on the pins in effect; not started with invalid ones.
    roofControl.begin(
        config, PinSetup::running(), PinSetup::valid(), RoofIo::pinSetupText(PinSetup::boot().reason, PinSetup::boot().unset));
    // Spec 010: which platform actions wait for the roof and the close sequence.
    // The close sequence's record (spec 005) is restored first, so its busy
    // flag is right before any request can ask.
    closeOnUnsafe.begin(config);
    PlatformGuard::begin(&roofControl, []() { return closeOnUnsafe.platformState(); }, []() { return WebServer::restartCommitted(); });
    HeapTrace::mark("roof");
    mountFilesystem();
    // Spec 017: the diagnostics log (only while on) and its kept summaries.
    diagnostics.begin(config, &roofControl);
    startWifi();
    startServices();
    startWhenOnline();

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
    diagnostics.passStarted(); // spec 017: timed only while diagnostics are on
    if (mqttClient)
        mqttClient->startPass(); // spec 008: MQTT's time in this pass

    wifiManager->setRetryPaused(webServer->isWifiConnectPending());
    wifiManager->handle();
    NetState::tick();  // spec 038: how long the network has been up
    startWhenOnline(); // spec 023

    // Joined a network from the setup hotspot: restart so time, MQTT, Alpaca
    // discovery and OTA start normally. The delay lets the setup screen show
    // the new address first.
    // Scheduled again if a close cancelled it (spec 010): the passwordless
    // hotspot, and the refusal to open while it is on, last until the restart.
    if (wifiManager->connectedFromHotspotFor(15000) && !WebServer::restartScheduled())
    {
        Logger::info("Main", "On a network while the setup hotspot is open; restarting");
        WebServer::scheduleRestart(1000);
    }

    // Command-line OTA blocks this loop for the whole upload and can't be
    // stopped part-way, so it is only serviced while the roof is closed: a
    // close can never be waiting behind it (spec 003 FR-030).
    if (arduinoOTAEnabled && RoofIo::commandLineOtaAllowed(roofControl.state()))
        ArduinoOTA.handle();
    if (timeManager)
        timeManager->handle();
    webServer->handle();
    HeapMap::upkeep(); // spec 038 reserve, spec 045 trend

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
    safetyPoller.setMountMayMove(roofControl.parkRequestAllowed()); // spec 050: checked again when a park is sent
    handleSafety();
    siteSync.handle(roofBusy());                      // spec 020
    safetyPoller.setRoofMoving(roofControl.moving()); // spec 047: the weather polled faster while moving
    WiringRun::handle();                              // spec 023
    diagnostics.handle();
    CrashLog::snapshot(Roof::stateName(roofControl.state()), roofControl.stackFreeBytes()); // spec 029
    diagnostics.passEnded();

    // Small delay to prevent a tight loop.
    delay(10);
}
