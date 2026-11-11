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

            // What command() would answer for an open or close now, without
            // changing anything (stop and abort are always accepted). Lets the
            // caller act on side effects (spec 003: a close stops an upload)
            // only for a command that will run.
            CommandResult check(Command command, Origin origin) const;

            // Spec 016: starts one leg of a calibration run (RoofControllerCalibrate.cpp).
            // Only a person's, at rest, with the readiness every move needs, a
            // leg within its ranges, and above the slow duty only from a
            // confirmed magnet (constitution I). The leg then runs through
            // tick() like any move, with the leg's limit as its timeout and
            // drive budget, and stops at the first magnet after it has left
            // its start magnet.
            CommandResult calibrate(const CalibrationLeg &leg, Origin origin);
            // What calibrate() would answer now, without changing anything.
            CommandResult checkCalibrate(const CalibrationLeg &leg, Origin origin) const;
            // How the last calibration leg ended.
            const CalibrationReport &calibrationReport() const { return legReport; }

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
                // Spec 016: a calibration leg, and what it has applied so far.
                bool calibration = false;
                CalibrationLeg leg;
                HallLevel startLevel = HallLevel::Unsettled;
                float dutyMs = 0.0f;
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
            void updateDeadTime(uint32_t nowMs);
            bool checkMagnets();
            bool checkLegMagnets();
            uint32_t budgetLimitMs() const;
            void endLeg(bool arrived);
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
            // Latched once the dead time has elapsed, so the stop's stamp is never
            // compared again: a long idle can't turn the difference negative and
            // bring the dead time back. Cleared by every stop and at construction.
            bool deadTimeDone = false;
            // Driven time toward each end (index: End) since a magnet was last
            // confirmed (FR-025). Per direction, so one reversal part-way (nearly
            // two travels between magnets) never trips it, while restarts in one
            // direction still do.
            uint32_t driveBudgetUsedMs[2] = {0, 0};
            Pending supplyLow;
            Pending overCurrent;
            Pending currentUnreadable;
            CalibrationReport legReport;
        };
    } // namespace Roof
} // namespace SQM
