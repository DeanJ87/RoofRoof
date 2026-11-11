#pragma once

// The automatic close on unsafe weather on the board (spec 005): gathers the
// weather verdict and park result (spec 004) and the roof's snapshot (spec
// 003) on the main loop, steps lib/CloseSequenceLogic's sequence, carries out
// its decision (a park request, or a close or stop with the automatic origin,
// which passes spec 003's gates like every other command), logs every change
// and hands the critical events to the alert sink, resending one that no
// channel took. Every decision is the library's (constitution V); nothing
// here can open the roof.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "AlertEngine.h"
#include "AlertRetry.h"
#include "CloseSequence.h"
#include "Config.h"
#include "RoofAlerts.h"

namespace SQM
{
    class RoofControl;
    class SafetyInputs;

    class CloseOnUnsafe
    {
    public:
        // Hands an alert to the dispatcher; returns its record id (0: not stored).
        using AlertSink = std::function<uint32_t(const Alerts::Alert &)>;
        // How the alert with that record id went.
        using DeliveryProbe = std::function<CloseSeq::Delivery(uint32_t)>;

        // setup(), before the main loop runs. Restores what was kept across a
        // restart: a stand-down, or an attempt with the time it has run.
        void begin(const Config &config);
        // Any task (a settings save runs on the web server's task).
        void updateConfig(const Config &config) { parkTimeoutMs.store(config.park.timeoutSeconds * 1000u); }
        // Main loop, before handle() first runs.
        void setAlertSink(AlertSink sink, DeliveryProbe probe)
        {
            alertSink = std::move(sink);
            deliveryProbe = std::move(probe);
        }
        // Main loop, every pass, after the safety inputs were handled.
        void handle(SafetyInputs &safety, RoofControl &roof);
        // Main loop: the roof as the alert engine sees it (spec 007), as of
        // the last step.
        const Alerts::RoofInputs &conditions() const { return roofConditions; }
        // Any task: the sequence as spec 010's hold rules see it (restarts,
        // uploads and WiFi changes wait while it works; see PlatformHold.h).
        RoofIo::SequenceState platformState() const { return CloseSeq::platformState(static_cast<CloseSeq::Phase>(phaseNow.load())); }
        // Any task: the sequence's phase as of its last step (spec 008: the
        // status document and MQTT).
        CloseSeq::Phase phase() const { return static_cast<CloseSeq::Phase>(phaseNow.load()); }

    private:
        static constexpr const char *TAG = "Close";
        static constexpr uint64_t STEP_EVERY_MS = 100;
        static constexpr uint64_t PROBE_EVERY_MS = 1000;
        // Added to the time since start, so a time kept across a restart can
        // be counted back from the first steps after it (a week).
        static constexpr uint64_t TIME_BASE_MS = 7ull * 24 * 3600 * 1000;

        CloseSeq::Decision carryOut(const CloseSeq::Decision &decision, SafetyInputs &safety, RoofControl &roof, std::string &refusal);
        void keep();
        void report(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons, const std::string &weather);
        void send(const Alerts::Alert &alert, uint64_t nowMs);
        void retryAlert(uint64_t nowMs);

        CloseSeq::Sequence sequence;
        std::atomic<uint8_t> phaseNow{static_cast<uint8_t>(CloseSeq::Phase::Idle)};
        std::atomic<uint32_t> parkTimeoutMs{CloseSeq::DEFAULT_PARK_TIMEOUT_MS};
        AlertSink alertSink;
        DeliveryProbe deliveryProbe;
        CloseSeq::AlertRetry retry;
        Alerts::Alert lastAlert; // the critical alert waiting for a channel to take it
        uint32_t lastAlertId = 0;
        uint64_t lastProbeMs = 0;
        CloseSeq::Phase loggedPhase = CloseSeq::Phase::Idle;
        bool stepped = false;
        uint64_t lastStepMs = 0;
        uint64_t stepNowMs = 0;       // the time base of the last step
        uint32_t personStopsSeen = 0; // in the snapshot the decision was taken from
        uint32_t keptSeq = 0;         // the sequence number of the newest record kept
        uint32_t closedCount = 0;     // closes after an attempt since start (spec 007)
        Alerts::RoofInputs roofConditions;
    };
} // namespace SQM
