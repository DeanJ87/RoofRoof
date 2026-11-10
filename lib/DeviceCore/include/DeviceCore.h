#pragma once

// What the device decides from its state and settings, independent of the
// hardware: alert decisions and wording, the imaging-app watch and the send
// schedule documents. The firmware (src/WebServer*.cpp), the native tests and
// the browser demo all run this. Spec 002 onwards adds the roof.

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "AlertEngine.h"
#include "AlertSchedule.h"
#include "AlpacaRouter.h"
#include "ClientWatch.h"
#include "Config.h"

namespace SQM
{
    namespace Core
    {
        // A clock reading before this (2024-01-01) means "not set yet".
        constexpr int64_t CLOCK_VALID_EPOCH = 1704067200;
        // ArduinoJson pool for /api/status (src/WebServerStatus.cpp): the
        // sections this library writes plus the device's own (firmware,
        // memory, partitions, time, WiFi, MQTT, connections), pinned by
        // test/test_device_core (test_status_document_fits).
        constexpr size_t STATUS_DOC_CAPACITY = 4096;

        // ISO 8601 UTC, or "" before the clock is set.
        std::string isoUtc(int64_t epoch);
        // "2 min", "45 s", "1 h 5 min".
        std::string formatDuration(uint32_t seconds);

        // Alerts.
        // The device state an alert pass reads.
        struct AlertSources
        {
            const Config &cfg;
        };
        // The device's local time ("HH:MM") and date for {time} and {date}.
        struct LocalClock
        {
            std::string time;
            std::string date;
        };
        Alerts::AlertRules alertRules(const Config &cfg);
        const AlertsConfig::EventSetting *eventSettingFor(const AlertsConfig &alerts, Alerts::AlertType type);
        // Template variables for an alert: the device's, then the event's own.
        std::vector<std::pair<std::string, std::string>> alertVars(
            const AlertSources &sources, const Alerts::Alert &alert, const LocalClock &clock);
        void applyAlertTemplate(
            Alerts::Alert &alert, const AlertsConfig::EventSetting &setting, const std::vector<std::pair<std::string, std::string>> &vars);

        // The imaging app (SQMeter spec 021): silence time per Alpaca device,
        // the client state as alert inputs, and the documents that report it.
        // `devices` is the Alpaca device list (Alpaca::Backend::devices).
        void clientSilenceMs(const Config &cfg, uint32_t (&out)[Alpaca::MAX_DEVICES]);
        // `localTime` is "HH:MM" when the clock is set (else anything else).
        void addClientInputs(
            Alerts::AlertInputs &inputs,
            const Alpaca::ClientWatch &watch,
            const std::vector<Alpaca::DeviceInfo> &devices,
            const Config &cfg,
            uint32_t nowMs,
            const std::string &localTime);
        // /api/status "alpaca": {enabled, clients: {<typeKey>: {...}}}.
        void writeClientWatch(
            JsonObject alpaca, const Alpaca::ClientWatch &watch, const std::vector<Alpaca::DeviceInfo> &devices, const Config &cfg, uint32_t nowMs);

        // When alerts go out: the setting, and the /api/alerts/armed document
        // (also /api/status "alerts").
        Alerts::SendMode sendMode(const Config &cfg);
        // The mode the schedule acts on: the saved one, or "any time" while Alpaca
        // is off (SQMeter spec 020 D-12).
        Alerts::SendMode effectiveSendMode(const Config &cfg);
        void writeAlertSchedule(JsonObject target, const Alerts::ScheduleState &state, const Config &cfg, uint32_t nowMs);

        // One alert pass: the engine's raw alerts given their configured
        // level, sound and wording; Off events dropped.
        struct AlertStep
        {
            std::vector<Alerts::Alert> outgoing;
        };
        AlertStep runAlerts(
            Alerts::AlertEngine &engine,
            const Alerts::AlertInputs &inputs,
            const Alerts::AlertRules &rules,
            const AlertSources &sources,
            const LocalClock &clock);

        // "Test" on an event row of the Alerts settings: a sample of each event.
        struct SampleAlert
        {
            const char *key; // event key, e.g. "client_lost"
            Alerts::AlertType type;
            const char *title; // built-in title
            const char *label; // "The imaging app stops checking"
        };
        const SampleAlert *sampleAlert(const std::string &key); // nullptr if unknown
        // The level, sound and wording chosen for a test of an event.
        struct TestWording
        {
            uint8_t level = 0;
            std::string sound;
            std::string title;
            std::string message;
        };
        // The test notification: the generic one (sample == nullptr) or a
        // sample of an event in the given level, sound and wording.
        Alerts::Alert buildTestAlert(
            const SampleAlert *sample, const TestWording &wording, const AlertSources &sources, const LocalClock &clock);
    } // namespace Core
} // namespace SQM
