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
            // succeed and switch the output off at once.
            CommandResult command(Command command, Origin origin, uint32_t nowMs);

            // A person clears the latched fault; automatic origins are refused.
            CommandResult clearFault(Origin origin);

            // Latches `fault` and switches the output off: a fault restored at
            // start-up, the watchdog (spec 003) or the park interlock (spec 005).
            // The first fault latched is kept until cleared.
            void raiseFault(const Fault &fault, uint32_t nowMs);

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
            bool checkMagnets();
            bool checkTimes(uint32_t nowMs);
            bool checkElectrical(const Readings &readings);
            bool confirmed(Pending &pending, bool condition, uint32_t nowMs) const;
            void driveProfile(uint32_t nowMs);
            void advanceEstimate(uint32_t elapsedMs);
            void arrive(uint32_t nowMs);

            // Commands, idle and the latch (RoofController.cpp).
            CommandResult requestMove(End target, Origin origin, uint32_t nowMs);
            CommandResult readinessRefusal() const;
            void stopMove(uint32_t nowMs, bool keepEstimate);
            void stopDriving(uint32_t nowMs);
            void latch(const Fault &fault, uint32_t nowMs);
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
            bool everDriven = false;
            uint32_t driveEndMs = 0;
            Pending supplyLow;
            Pending overCurrent;
            Pending currentUnreadable;
        };
    } // namespace Roof
} // namespace SQM
