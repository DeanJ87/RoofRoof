#pragma once

// The roof's state machine (spec 002, contracts/roof-logic.md): commands, the
// per-step move with its profile, timeout and fault checks, the position
// estimate and the fault latch. Pure: time and readings come in, a driver
// command and duty go out. Spec 003 steps it at a fixed rate and applies
// output() through lib/DriverLogic.

#include <cstdint>

#include "HallFilter.h"
#include "RoofSettings.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace Roof
    {
        // The roof controller. One instance, owned by the motion task.
        class Controller
        {
        public:
            explicit Controller(const Settings &settings = Settings());

            // One control step; returns the output to apply now.
            Output tick(const Readings &readings);

            // Open, close, stop or abort between steps. Stop and abort always
            // succeed and switch the output off at once. Commands carry no time:
            // every time the logic uses (the dead time, timeouts, the estimate)
            // comes from tick() readings, so a command from another task with a
            // skewed clock can't shorten the dead time.
            CommandResult command(Command command, Origin origin);

            // A person clears the latched fault; automatic origins are refused.
            CommandResult clearFault(Origin origin);

            // Latches `fault` and switches the output off: a fault restored at
            // start-up, the watchdog (spec 003) or the park interlock (spec 005).
            // The first fault latched is kept until cleared.
            void raiseFault(const Fault &fault);

            // New settings, refused (Refusal::Moving) while a move is active. Invalid
            // settings are taken but refuse every move until valid ones arrive.
            CommandResult applySettings(const Settings &settings);

            const Status &status() const { return current; }
            Output output() const { return out; }
            const Settings &settings() const { return config; }

        private:
            // The move in progress.
            struct Move
            {
                bool active = false;
                End target = End::Closed;
                bool driving = false;         // the driver is on (after the dead time)
                bool fromUnknown = false;     // slow, to the first magnet
                bool startedAtMagnet = false; // the drive began on the other end's magnet
                bool leftStart = false;       // and has since left it
                uint32_t driveStartMs = 0;
                uint32_t timeoutMs = 0;
            };

            // When a condition that must last CONFIRM_MS was first seen.
            struct Pending
            {
                bool active = false;
                uint32_t sinceMs = 0;
            };

            // Move decisions (RoofControllerStep.cpp).
            void stepMove(const Readings &readings);
            void startDriving(uint32_t nowMs);
            bool waitingDeadTime(uint32_t nowMs) const;
            bool checkMagnets();
            bool checkTimes(uint32_t nowMs);
            bool checkElectrical(const Readings &readings);
            bool confirmed(Pending &pending, bool condition, uint32_t nowMs) const;
            void driveProfile(uint32_t nowMs);
            void advanceEstimate(uint32_t elapsedMs);
            void arrive();

            // Commands, idle and the latch (RoofController.cpp).
            CommandResult requestMove(End target, Origin origin);
            CommandResult readinessRefusal() const;
            void stopMove(bool keepEstimate);
            void stopDriving();
            void latch(const Fault &fault);
            void followHall();
            void settleIdle();
            bool magnetEnd(End &end) const;
            float remainingMm() const;
            void resetPending();

            Settings config;
            bool settingsValid;
            HallFilter hall;
            Status current;
            Output out;
            Move move;
            Readings last;
            bool stepped = false;
            uint32_t lastStepMs = 0;
            // The dead time runs from the first step after the driver went off;
            // construction counts as such a stop (constitution I: off after boot).
            bool driveEndPending = true;
            uint32_t driveEndMs = 0;
            // Driven time toward each end (index: End) since a magnet was last
            // confirmed (FR-025). Per direction, so one reversal part-way (nearly
            // two travels between magnets) never trips it, while restarts in one
            // direction still do.
            uint32_t driveBudgetUsedMs[2] = {0, 0};
            Pending supplyLow;
            Pending overCurrent;
            Pending currentUnreadable;
        };
    } // namespace Roof
} // namespace SQM
