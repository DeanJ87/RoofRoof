#pragma once

// The roof on the board (spec 003): spec 002's Roof::Controller stepped every
// 20 ms by its own task, fed from the Hall, supply and current ADC inputs,
// applied to the IBT-2 through MotorDriver, watched by a timer that drops EN
// if the stepping stalls, with its latched fault kept in NVS. Every decision
// is in lib/RoofLogic and lib/RoofIoLogic; this only reads pins, takes the
// lock and calls them (constitution I, V).

#include <atomic>
#include <cstdint>
#include <esp_timer.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string>

#include "BenchMode.h"
#include "CalibrationRun.h"
#include "CommandCheck.h"
#include "Config.h"
#include "FaultRecord.h"
#include "MotionGuards.h"
#include "ParkRule.h"
#include "PlatformHold.h"
#include "RoofController.h"
#include "RoofGates.h"
#include "RoofStatus.h"
#include "SafetyInput.h"
#include "SwitchInput.h"
#include "DiagDocuments.h"
#include "MoveLog.h"
#if ROOFROOF_BENCH
#include "BenchGuard.h"
#include "RoofPlant.h"
#endif

namespace SQM
{
    class RoofControl
    {
    public:
        // The answer to a command, for the REST reply and the log.
        struct Outcome
        {
            bool accepted = true;
            std::string reason; // "" or a gate/refusal machine name
            std::string text;   // why it was refused, with the value and the limit
            Roof::State state = Roof::State::Unknown;
            bool pending = false; // a close accepted, waiting for an upload or MQTT connect to stop
        };

        RoofControl() = default;
        RoofControl(const RoofControl &) = delete;
        RoofControl &operator=(const RoofControl &) = delete;

        // setup(), after the config is loaded: the controller with the saved
        // settings and any saved fault latched, the PWM and inputs, then the
        // control task and the watchdog timer. False if the task or timer
        // couldn't start (the driver then stays off).
        bool begin(const Config &config);

        // Any task. Open and close pass the weather and park gates first. A
        // stop is refused only for an Alpaca client while a close runs or waits
        // in weather that isn't safe (spec 006); a person's never is. The output is applied before this returns.
        Outcome command(Roof::Command command, Roof::Origin origin);
        // Spec 005's automatic close: refused (CloseSeq::PERSON_STOP_REASON)
        // if a person has stopped the roof since the snapshot that showed
        // `personStopsSeen`, checked under the same lock as the command, so
        // the close can never start after a person's stop it hasn't seen.
        Outcome automaticClose(uint32_t personStopsSeen);
        Outcome clearFault(Roof::Origin origin);
        // Spec 016 (src/RoofCalibration.cpp): a person starts a calibration run
        // (refused unless every check passes; `sequenceIdle` is spec 005's
        // sequence as PlatformGuard sees it), stops it (a person's stop, which
        // ends any run), and reads a copy of its report.
        Outcome startCalibration(const Calib::Request &request, const std::function<bool()> &sequenceIdle);
        Outcome stopCalibration();
        // The person confirms (or denies) that the slow open reached the open end.
        Outcome confirmCalibration(bool atOpenEnd);
        Calib::Report calibrationReport(uint32_t &nowMs) const;
        // Any task: a run is under way (pauses included).
        bool calibrating() const { return calibratingNow.load(); }

        // Any task: latch an External fault with this cause (the output goes off).
        void raiseFault(RoofIo::FaultCause cause, float measured, float limit);

        // Any task: a save of `next` over `current` changes motion settings while the roof moves.
        bool settingsSaveRefused(const Config &current, const Config &next) const;
        // Any task: the saved config; the control task hands it to the logic
        // once no move is active.
        void updateConfig(const Config &config);

        // Main loop, after SafetyInputs::handle(): what the gates look at,
        // with whether daylight wants the roof closed (spec 013) and why.
        void setSafety(
            const Safety::WeatherVerdict &weather,
            const Safety::ParkResult &park,
            bool daylightCloses = false,
            const std::string &daylightText = "");
        // Main loop: the switch and contact readings, for SafetyInputs.
        Safety::SwitchReading parkSwitch(uint8_t index) const;
        Safety::RainContact rainContact() const;

        // Any task: the roof is opening or closing. Flash writes (settings,
        // uploads, NVS) and restarts wait while it is (RoofIo::platformActionRefused).
        bool moving() const;
        Roof::State state() const { return static_cast<Roof::State>(stateNow.load()); }
        // A close is waiting for an upload or MQTT connect (MQTT reconnects wait meanwhile).
        bool closeWaiting() const { return closeWaitingNow.load(); }
        // Any task (spec 010): under the roof's lock, if the roof isn't moving
        // and no close waits, runs `action` (may be empty) and returns None;
        // otherwise Moving or CloseWaiting. Commands decide under the same lock
        // and probe their blockers there, so no move starts between this check
        // and `action`, and a command decided after it sees what `action`
        // registered (a flash write, an upload, a committed restart). `action`
        // must not block. With `wait` false the lock isn't waited for: Moving
        // if a step or command holds it (try again).
        RoofIo::HoldReason whileStill(const std::function<void()> &action, bool wait = true);
        // Only inside a whileStill action (the lock is held): a latched fault
        // isn't in NVS yet, so a restart now would lose the lockout.
        bool faultUnsavedHeld() const;
        // What else is going on (uploads, an MQTT connect, a scheduled
        // restart), and what a close does about it (cancel the restart, stop
        // the upload). Set once, before any command can arrive.
        // Under the roof's lock (the control task calls them; spec 010).
        void setBlockers(std::function<RoofIo::Blockers()> probe, std::function<void(const RoofIo::BlockerDecision &)> onClose);

        // Any task: the control task and watchdog started (else nothing can move).
        bool running() const { return started.load(); }
        // Any task.
        RoofIo::Snapshot snapshot() const;
        // Any task: the snapshot plus the weather and park as a command's checks
        // see them now (spec 008: the status and MQTT documents).
        RoofIo::Snapshot snapshotWithChecks() const;
        uint32_t revision() const { return revisionCount.load(); }
        // The control task's unused stack, bytes (0 before it starts).
        uint32_t stackFreeBytes() const;

        // Spec 017, advanced diagnostics (src/RoofControlDiag.cpp). The main
        // loop owns the log's memory: attachDiag() hands it to the step under
        // the lock, detachDiag() takes it back under the lock (then it may be
        // freed). The step records into it after applying its output; with no
        // log attached the step does nothing for diagnostics and isn't timed.
        void attachDiag(Diag::MoveLog *log);
        Diag::MoveLog *detachDiag();
        // Any task: bounded copies under the lock. False when no log is
        // attached (diagLive still fills the roof's part).
        bool diagLive(Diag::RoofLive &out) const;
        // The read interface for other firmware code (research R13): one held
        // move with its profile, the move in progress or the last one.
        bool diagMove(uint32_t id, Diag::Move &out) const;
        bool diagLatest(Diag::Summary &out) const;
        uint8_t diagIds(uint32_t (&out)[Diag::MAX_MOVES]) const;
        // Moves finished since the log was attached (0 without one).
        uint32_t diagFinished() const;
        // A person clears the log: refused (Moving or CloseWaiting) while the
        // roof moves or a close waits, so the move in progress isn't lost.
        RoofIo::HoldReason diagClear();
        // The control step's own duration, last and longest (µs; -1 untimed).
        int64_t diagStepUs() const { return diagTimed.load() ? static_cast<int64_t>(stepUsNow.load()) : -1; }
        int64_t diagMaxStepUs() const { return diagTimed.load() ? static_cast<int64_t>(maxStepUsNow.load()) : -1; }

    private:
        static constexpr const char *TAG = "Roof";
        static constexpr uint32_t TASK_STACK_BYTES = 6144;
        static constexpr UBaseType_t TASK_PRIORITY = 5; // above the loop (1) and AsyncTCP (3, set in platformio.ini), below esp_timer (22)
        static constexpr BaseType_t TASK_CORE = 1;

        // The settings this layer uses, as handed between tasks.
        struct HardwareSettings
        {
            RoofConfig roof;
            uint8_t fittedSwitches = 0;
            bool rainFitted = false;
        };

        // One step's readings beyond the logic's.
        struct StepInputs
        {
            Roof::Readings readings;
            uint32_t gapMs = 0;
        };

        static HardwareSettings settingsOf(const Config &config);
        bool startTask();
        static void taskEntry(void *self);
        static void watchdogEntry(void *self);
        void run();
        void step();
        StepInputs readInputs(uint32_t nowMs);
        void setupPins();
        void restoreFault();
#if ROOFROOF_BENCH
        // Bench mode (spec 011, src/RoofBench.cpp): the simulated roof and the
        // real-supply guard. benchStart() before the task starts; benchStep()
        // every step under the lock, replacing the ADC's readings.
        void benchStart();
        void benchStep(StepInputs &in);
        void benchSettings();
        void benchLatch();
#endif

        // Under the lock.
        void stepLocked(const StepInputs &in);
        void recordDiag(const StepInputs &in);
        void noteStepTime(int64_t us);
        void takePendingSettings();
        RoofIo::GateInputs currentGates() const;
        void checkGates();
        void latchExternal(const RoofIo::FaultRecord &record);
        void applyOutput();
        void syncFault(RoofIo::FaultCause causeIfNew);
        void refreshSnapshot();
        Outcome outcomeOf(const Roof::CommandResult &result);
        Outcome runLocked(Roof::Command command, Roof::Origin origin);
        Outcome precheck(Roof::Command command, Roof::Origin origin) const;
        RoofIo::CommandCheck checkNow(Roof::Command command, Roof::Origin origin) const;
        Outcome runChecked(Roof::Command command, Roof::Origin origin);
        static Outcome blockedOutcome(const RoofIo::Blockers &now, Roof::State state);
        Outcome commandLocked(Roof::Command command, Roof::Origin origin, const RoofIo::Blockers &now);
        Outcome stopLocked(Roof::Command command, Roof::Origin origin);
        void runPendingClose();
        void stepCalibration(const Roof::Readings &readings);
        void endCalibrationFor(bool yieldToClose, const std::string &text);

        // Outside the lock, in the control task.
        void persistFault();
        void logChanges();

        mutable SemaphoreHandle_t lock = nullptr;
        TaskHandle_t task = nullptr;
        esp_timer_handle_t watchdogTimer = nullptr;

        // Guarded by `lock`.
        Roof::Controller controller;
        HardwareSettings settings;
        bool settingsPending = false;
        HardwareSettings pendingSettings;
        RoofIo::GateInputs gates;
        bool closePending = false; // a close waiting for the flash or an MQTT connect (close wins)
        // A person's stops and aborts since start (spec 005; never an Alpaca client's, spec 006, unless autoClose.remoteStop, spec 014).
        uint32_t personStops = 0;
        uint32_t remoteStops = 0; // spec 014: those of personStops that were a remote stop honoured by autoClose.remoteStop
        Roof::Origin closeOrigin = Roof::Origin::Person;
        bool safetyHandedOver = false;
        uint32_t safetyAtMs = 0;
        RoofIo::FaultRecord fault;      // the latched fault and its cause
        RoofIo::FaultRecord savedFault; // what NVS holds
        RoofIo::Snapshot snap;
        float supplyVolts = 0.0f; // the last step's readings
        float motorAmps = 0.0f;
        uint32_t watchdogTrips = 0;
        uint32_t maxStepGapMs = 0;
        uint32_t maxBeatAgeMs = 0; // the oldest watchdog heartbeat a step has seen
        std::string notice;        // logged by the control task outside the lock
        Calib::Run calibration;    // spec 016: the attended calibration run
        Diag::MoveLog *diagLog = nullptr;               // spec 017: owned by the main loop's Diagnostics
        Roof::Origin moveOrigin = Roof::Origin::Person; // of the last accepted open or close

#if ROOFROOF_BENCH
        // Guarded by `lock`.
        RoofPlant::Plant benchPlant;
        RoofPlant::BenchSupplyGuard benchGuard;
#endif

        // The control task only.
        RoofIo::SwitchInput park1;
        RoofIo::SwitchInput park2;
        RoofIo::SwitchInput rain;
        Roof::State loggedState = Roof::State::Unknown;
        Roof::FaultKind loggedFault = Roof::FaultKind::None;
        uint32_t saveFailedAtMs = 0; // when saving the fault last failed (0: it didn't)

        // Shared without the lock.
        std::atomic<uint32_t> lastStepMs{0};
        std::atomic<uint32_t> revisionCount{0};
        std::atomic<uint8_t> park1Reading{0}; // RoofIo::Contact
        std::atomic<uint8_t> park2Reading{0};
        std::atomic<uint8_t> rainReading{0};
        std::atomic<bool> started{false};
        std::atomic<bool> remoteStopsClose{false}; // spec 014: autoClose.remoteStop
        std::atomic<uint8_t> stateNow{0};          // Roof::State, as of the last snapshot
        std::atomic<bool> closeWaitingNow{false};
        std::atomic<bool> calibratingNow{false}; // spec 016: calibration.active(), for moving()
        std::function<RoofIo::Blockers()> blockers;
        std::function<void(const RoofIo::BlockerDecision &)> closeHook;
        // Spec 017: set with the log; the control task times its steps while set.
        std::atomic<bool> diagTimed{false};
        std::atomic<uint32_t> stepUsNow{0};    // written by the control task only
        std::atomic<uint32_t> maxStepUsNow{0};
    };
} // namespace SQM
