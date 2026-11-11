#pragma once

// The RoofRoof firmware's own decision code (lib/), running in the browser for
// the demo. One EmulatedDevice holds what the device would hold - settings,
// the alert engine and schedule, and the Alpaca API - and answers the same
// requests with the same documents. No I/O: the demo feeds it the time
// (SQMeter spec 016). Spec 011 adds the simulated roof.

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "AlertDelivery.h"
#include "AlertEngine.h"
#include "AlpacaRouter.h"
#include "Config.h"
#include "ConfigBackup.h"
#include "DeviceCore.h"
#include "SettingsDeps.h"

using namespace SQM;

namespace DemoCore
{
    inline constexpr size_t MAX_RECORDS = 20; // AlertDispatcher::MAX_RECORDS
    inline const char *const CHANNEL_NAMES[] = {"mqtt", "pushover", "ntfy", "webhook"};
    inline const char *const DEMO_DETAIL = "Demo: nothing was sent";

    inline std::string errorJson(const std::string &message)
    {
        StaticJsonDocument<512> doc;
        doc["error"] = message;
        std::string out;
        serializeJson(doc, out);
        return out;
    }

    struct Record
    {
        uint32_t id = 0;
        uint32_t uptimeSeconds = 0;
        int64_t epochSeconds = 0;
        Alerts::Alert alert;
        bool channel[4] = {false, false, false, false};
        uint8_t requested = 0x0F;                                      // channels asked for: all, or the one a per-channel test named
        const char *skipped[4] = {nullptr, nullptr, nullptr, nullptr}; // switched on but inactive: why
    };
} // namespace DemoCore

using DemoCore::Record;

class EmulatedDevice : public Alpaca::Backend
{
public:
    // identityJson: {"version": "...", "mac": "a1b2c3d4e5f6"}
    explicit EmulatedDevice(const std::string &identityJson)
        : router(*this, identity(identityJson)),
          firmwareVersion(identity(identityJson).version)
    {
        cfg = Config::createDefault();
    }

    // --- Alpaca::Backend ---------------------------------------------------
    bool alpacaEnabled() const override { return cfg.alpaca.enabled; }
    std::vector<Alpaca::DeviceInfo> devices() const override { return {}; } // the Dome comes with spec 006
    std::string serverName() const override { return cfg.deviceName; }
    std::string location() const override { return ""; }
    std::string timestampUtc() const override { return Core::isoUtc(epoch); }

    // --- Settings ----------------------------------------------------------
    std::string getConfig(bool redacted) const { return cfg.toJson(redacted); }

    // POST /api/config. Same validation and messages as the device.
    std::string applyConfig(const std::string &json);

    // Restores saved demo state without the "changed by the UI" rules.
    bool loadConfig(const std::string &json);

    // POST /api/restart (applied by the demo after a short pause).
    void restart(double nowMsIn);

    // --- One pass of the device's loop -------------------------------------
    void tick(double nowMsIn, double epochIn, const std::string &localTime, const std::string &localDate);

    // --- Documents ---------------------------------------------------------
    // The decision parts of GET /api/status; the demo adds the hardware parts.
    std::string statusParts() const;

    // GET /api/settings/effective: what's actually in effect, from the
    // device's own dependency rules (SQMeter spec 020).
    std::string effective() const;

    // Alerts the device is holding back (the demo panel's waits).
    std::string pending() const;

    // GET /api/alerts/recent
    std::string recentAlerts() const;

    void clearAlerts() { records.clear(); }

    // --- Pause / resume (SQMeter spec 021) -----------------------------------
    bool isArmed() const { return schedule.state().sending; }
    // GET /api/alerts/armed
    std::string armedDocument() const;

    // POST /api/alerts/arm|disarm (source "ui" or "rest") and MQTT ("mqtt").
    void setArmed(bool on, const std::string &source);

private:
    int64_t validEpoch() const { return epoch >= Core::CLOCK_VALID_EPOCH ? epoch : 0; }

    void scheduleChanged(bool wasSending);

public:
    // POST /api/alerts/test?channel=&event=&level=&sound=&title=&message=
    // Returns {status, body} like the device.
    std::string testAlert(const std::string &paramsJson);

    // Demo panel "Send a test" for real notifications (SQMeter spec 018): a
    // test alert recorded whatever the device's own channel switches say.
    void realTestAlert();

    // The requests a real RoofRoof would send for alert `id`, for the
    // channels the visitor set up. Wake is sent as Urgent: Pushover's
    // emergency level needs acknowledging.
    std::string deliveryRequests(uint32_t id, const std::string &credentialsJson) const;

    // --- Alpaca --------------------------------------------------------------
    // method "GET"/"PUT", path, params as [[name, value], ...].
    std::string alpaca(const std::string &method, const std::string &path, const std::string &paramsJson);

    // --- Backup and restore (SQMeter spec 033) ---------------------------------
    // POST /api/backup. randomHex: 28 random bytes (salt, nonce). Returns {status, body}.
    std::string backup(const std::string &body, const std::string &randomHex);
    // POST /api/backup/preview. randomHex: 16 random bytes (the token).
    std::string restorePreview(const std::string &body, const std::string &randomHex);
    // POST /api/backup/apply. The demo restarts itself when "restarting" is true.
    std::string restoreApply(const std::string &body);
    // The three above by route ("backup", "preview", "apply"): one binding for the demo.
    std::string backupRoute(const std::string &route, const std::string &body, const std::string &randomHex);

    // --- Persistence (sessionStorage) -----------------------------------------
    std::string saveState() const;

    bool loadState(const std::string &json);

private:
    static Alpaca::ServerIdentity identity(const std::string &json);
    Backup::DeviceInfo backupDevice() const;

    // The previewed restore, until applied (the device's StagedRestore).
    struct StagedRestore
    {
        std::string token;
        Config candidate;
        std::string from; // the settings it was previewed on
        bool restart = false;
    };
    StagedRestore staged;

    static std::string serialize(const JsonDocument &doc);

    static std::string response(int status, const std::string &body);

    uint32_t uptimeSeconds() const { return (nowMs - bootMs) / 1000; }

    static void writeRequest(JsonObject target, const Delivery::HttpRequest &request);

    // The demo's network is simulated: WiFi is up, and the broker is
    // "connected" whenever MQTT is on.
    Deps::Facts facts() const;

    uint8_t enabledChannels() const;

    void record(const Alerts::Alert &alert, uint8_t mask);

    void runAlerts();

    Config cfg;
    Alerts::AlertEngine engine;
    Alerts::AlertSchedule schedule;
    Alpaca::ClientWatch clientWatch;
    std::deque<Record> records;
    uint32_t nextId = 1;
    uint32_t nowMs = 0;
    uint32_t bootMs = 0;
    int64_t epoch = 0;
    std::string clockTime = "--:--";
    std::string clockDate = "--";
    Alpaca::Router router;
    std::string firmwareVersion;
};
