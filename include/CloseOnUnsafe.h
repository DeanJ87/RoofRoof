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
#include <mutex>
#include <string>

#include "AlertEngine.h"
#include "AlertRetry.h"
#include "CommandCheck.h"
#include "NeedsPerson.h"
#include "CloseSequence.h"
#include "Config.h"
#include "Daylight.h"
#include "RoofAlerts.h"
#include "NinaLost.h"
#include "WeatherHold.h"

namespace SQM
{
    class RoofControl;
    class SafetyInputs;

    class CloseOnUnsafe
    {
    public:
        // Spec 027: a command waiting for the park gave up: the critical
        // "mount not parked" alert with its text.
        void parkWaitFailed(const std::string &text);
        // Spec 039: a person's Open or Close set the park check aside: the information alert.
        void parkOverridden(const std::string &text);
        // Spec 047: a move finished without a fresh park reading (information).
        void finishedOnStale(const std::string &text);
        // Spec 030: where the critical alerts go (the alert outbox): queued until
        // a channel takes them (`resolvable`: dropped when the condition ends),
        // resolved when it ends, and the longest gap between two sends.
        struct AlertOut
        {
            std::function<void(const Alerts::Alert &, bool resolvable)> queue;
            std::function<void(Alerts::AlertType)> resolve;
            std::function<void(uint64_t)> maxGap;
        };

        // setup(), before the main loop runs. Restores what was kept across a
        // restart: a stand-down, or an attempt with the time it has run.
        void begin(const Config &config);
        // Any task (a settings save runs on the web server's task).
        void updateConfig(const Config &config);
        // Main loop, before the gates are handed the verdict: works the
        // daylight state out about once a second (spec 013).
        // Spec 022: `testBox`, the test box's daylight bypass in effect (no close, no open refusal).
        void updateDaylight(bool testBox = false);
        // Main loop: the daylight state as of the last update.
        const Daylight::View &daylight() const { return daylightView; }
        // Any task: the daylight state for the status document.
        RoofIo::DaylightStatus daylightStatus() const;
        // Main loop, before handle() first runs.
        void setAlertOut(AlertOut out) { alertOut = std::move(out); }
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
        // Spec 026, any task: the stand-down's seconds left if nothing wants the
        // roof closed meanwhile (-1: not counting down), as of the last step.
        // Spec 044: with whether they count down to a person's stop lapsing
        // (the close then starts), not to the safe spell.
        RoofIo::CloseRearm standDown() const { return {standDownLeftS.load(), standDownLapsing.load()}; }
        // Spec 030, any task: the weather hold as of the last step (status document).
        RoofIo::WeatherHoldStatus weatherHoldStatus() const;
        // Spec 052, any task: the automatic close is armed (the hold has fired
        // and not yet cleared), as of the last step: a remote Open is refused.
        RoofIo::CloseArmed closeArmed() const { return {armedNow.load(), armedLeftMs.load(), armedHoldMs.load()}; }
        // Spec 030, any task: the roof needs a person (the status, MQTT and the
        // banner), as of the last step.
        RoofIo::NeedsPersonStatus needsPersonStatus() const;
        // Spec 026, any task: a person's attended release of a stand-down,
        // applied by the next step (the sequence checks again there). Refused
        // (nullptr: accepted) when there is none, or something wants the roof closed.
        const char *requestRelease();

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
        void countClosed(CloseSeq::Phase before, const CloseSeq::Decision &decision);
        void logHoldEnd(CloseSeq::Phase before, const CloseSeq::Inputs &in) const;
        void updateConditions(const RoofIo::Snapshot &snap, SafetyInputs &safety);
        void holdWeather(CloseSeq::Inputs &in, uint8_t overrideBits, uint32_t reasons);
        void shareStandDown(); // spec 026/044: the countdown for the status
        void watchNina(CloseSeq::Inputs &in, const RoofIo::Snapshot &snap, const Safety::ParkResult &park);
        void send(const Alerts::Alert &alert, bool resolvable = true);
        void settleAlerts(const RoofIo::Snapshot &snap, bool roofStarted); // spec 030

        CloseSeq::Sequence sequence;
        std::atomic<uint8_t> phaseNow{static_cast<uint8_t>(CloseSeq::Phase::Idle)};
        std::atomic<uint32_t> parkTimeoutMs{CloseSeq::DEFAULT_PARK_TIMEOUT_MS};
        std::atomic<int32_t> standDownLeftS{-1};
        std::atomic<bool> standDownLapsing{false};  // spec 044
        std::atomic<bool> nothingWantsClose{false}; // the last step: weather safe and no daylight close
        std::atomic<bool> releaseWanted{false};
        std::atomic<bool> armedNow{false}; // spec 052: closeArmed()
        std::atomic<uint32_t> armedLeftMs{0};
        std::atomic<uint32_t> armedHoldMs{0};
        AlertOut alertOut;
        // Spec 030: the needs-a-person watcher (main loop) and its status copy.
        CloseSeq::NeedsPerson needsPerson;
        bool needsPersonHeld = false; // the last step: a needs_person condition held
        mutable std::mutex needsPersonLock;
        RoofIo::NeedsPersonStatus needsPersonShown;
        CloseSeq::Phase loggedPhase = CloseSeq::Phase::Idle;
        bool stepped = false;
        uint64_t lastStepMs = 0;
        uint64_t stepNowMs = 0;       // the time base of the last step
        uint32_t personStopsSeen = 0; // in the snapshot the decision was taken from
        uint32_t keptSeq = 0;         // the sequence number of the newest record kept
        uint32_t closedCount = 0;     // closes after a weather attempt since start (spec 007)
        // Spec 013: the daylight state, worked out on the main loop.
        static constexpr uint32_t DAYLIGHT_EVERY_MS = 1000;
        mutable std::mutex daylightLock; // the settings (any task) and the status copy
        Daylight::Settings daylightSettings;
        CloseSeq::Policy policy; // spec 014: autoClose.*, under daylightLock
        RoofIo::DaylightStatus daylightShown;
        Daylight::Watch daylightWatch;
        Daylight::View daylightView;
        bool daylightStepped = false;
        uint32_t lastDaylightMs = 0;
        uint32_t daylightClosedCount = 0; // daylight episodes that ended closed since start
        Alerts::RoofInputs roofConditions;
        CloseSeq::WeatherHold weatherHold;     // spec 030, main loop
        CloseSeq::NinaLostWatch ninaLostWatch; // spec 030 (autoClose.ninaLostClose), main loop
        RoofIo::WeatherHoldStatus holdShown;   // its status copy, under daylightLock
    };
} // namespace SQM
