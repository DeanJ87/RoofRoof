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

#include "Config.h"
#include "FaultRecord.h"
#include "MotionGuards.h"
#include "ParkRule.h"
#include "RoofController.h"
#include "RoofGates.h"
#include "RoofStatus.h"
#include "SafetyInput.h"
#include "SwitchInput.h"

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

        // Any task. Open and close pass the weather and park gates first; stop
        // is never refused. The output is applied before this returns.
        Outcome command(Roof::Command command, Roof::Origin origin);
        // Spec 005's automatic close: refused (CloseSeq::PERSON_STOP_REASON)
        // if a person has stopped the roof since the snapshot that showed
        // `personStopsSeen`, checked under the same lock as the command, so
        // the close can never start after a person's stop it hasn't seen.
        Outcome automaticClose(uint32_t personStopsSeen);
        Outcome clearFault(Roof::Origin origin);
        // Any task: latch an External fault with this cause (the output goes off).
        void raiseFault(RoofIo::FaultCause cause, float measured, float limit);

        // Any task: a save of `next` over `current` changes motion settings while the roof moves.
        bool settingsSaveRefused(const Config &current, const Config &next) const;
        // Any task: the saved config; the control task hands it to the logic
        // once no move is active.
        void updateConfig(const Config &config);

        // Main loop, after SafetyInputs::handle(): what the gates look at.
        void setSafety(const Safety::WeatherVerdict &weather, const Safety::ParkResult &park);
        // Main loop: the switch and contact readings, for SafetyInputs.
        Safety::SwitchReading parkSwitch(uint8_t index) const;
        Safety::RainContact rainContact() const;

        // Any task: the roof is opening or closing. Flash writes (settings,
        // uploads, NVS) and restarts wait while it is (RoofIo::platformActionRefused).
        bool moving() const;
        Roof::State state() const { return static_cast<Roof::State>(stateNow.load()); }
        // A close is waiting for an upload or MQTT connect (MQTT reconnects wait meanwhile).
        bool closeWaiting() const { return closeWaitingNow.load(); }
        // What else is going on (uploads, an MQTT connect, a scheduled
        // restart), and what a close does about it (cancel the restart, stop
        // the upload). Set once, before any command can arrive.
        void setBlockers(std::function<RoofIo::Blockers()> probe, std::function<void(const RoofIo::BlockerDecision &)> onClose)
        {
            blockers = std::move(probe);
            closeHook = std::move(onClose);
        }

        // Any task: the control task and watchdog started (else nothing can move).
        bool running() const { return started.load(); }
        // Any task.
        RoofIo::Snapshot snapshot() const;
        uint32_t revision() const { return revisionCount.load(); }
        // The control task's unused stack, bytes (0 before it starts).
        uint32_t stackFreeBytes() const;

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

        // Under the lock.
        void stepLocked(const StepInputs &in);
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
        Outcome commandLocked(Roof::Command command, Roof::Origin origin, const RoofIo::Blockers &now);
        void runPendingClose();

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
        uint32_t personStops = 0;  // a person's or an Alpaca client's stops and aborts since start (specs 005, 006)
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
        std::atomic<uint8_t> stateNow{0}; // Roof::State, as of the last snapshot
        std::atomic<bool> closeWaitingNow{false};
        std::function<RoofIo::Blockers()> blockers;
        std::function<void(const RoofIo::BlockerDecision &)> closeHook;
    };
} // namespace SQM
