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
        // The raw Hall readings of the last steps, oldest first.
        struct HallTrail
        {
            uint16_t counts[HALL_TRAIL_READINGS] = {};
            uint8_t count = 0;
        };

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
            // Faults latched since construction: a run notices one even if a
            // person cleared it before the run's next step (spec 016).
            uint32_t faultCount() const { return faultsLatched; }

            // Spec 019: a person's jog toward `target` (RoofControllerCalibrate.cpp):
            // a calibration leg at the slow duty that stops at the first magnet
            // after leaving its start magnet, with jogLimitMs() as its hard
            // timeout and every move's drive budget. It runs only while renewed:
            // the step stops it once JOG_HOLD_MS of step time pass without a
            // holdJog(). Refused as calibrate() refuses a leg.
            // Spec 033 (constitution 9.0.0, I): a person's jog while the Hall
            // sensor can't place the roof (the Hall rail fault latched and no
            // other fault, or the reading at a rail or not settled) runs blind
            // instead (RoofControllerBlind.cpp): toward `target` at the slow
            // duty on time alone, while renewed, up to jogLimitMs(), stopped by
            // a stall, Stop, Abort or any fault the blind close stops on. It
            // needs valid settings and power only; the fault stays latched and
            // the position unknown. Status::jogging is set, Status::blind isn't.
            CommandResult jog(End target, Origin origin);
            CommandResult checkJog(End target, Origin origin) const;
            // Renews the jog `id` (from jogId() after its start): false, and
            // nothing changes, when no jog or another one is active.
            bool holdJog(uint32_t id);
            // The person lets go of jog `id`: stops it as a Stop does (true), or
            // nothing when no jog or another one is active (false).
            bool releaseJog(uint32_t id);
            uint32_t jogId() const { return jogCount; }
            // The smaller of JOG_MAX_MS and a move from an unknown position's timeout.
            uint32_t jogLimitMs() const;

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
            // Driven time toward `end` since a magnet was last confirmed (FR-025's
            // budget, read-only: spec 017's diagnostics report it).
            uint32_t budgetUsedMs(End end) const { return driveBudgetUsedMs[static_cast<int>(end)]; }
            // The raw Hall readings up to the step the latched fault was taken on
            // (empty before any fault): a wrong magnet from noise shows as a burst.
            const HallTrail &faultHall() const { return faultTrail; }
            // Spec 027: an Open or Close of a roof already resting at that end
            // (no move under way): nothing to do.
            bool atEnd(Command command) const;
            // Spec 027: the latched fault is a Hall rail reading taken with no
            // move under way (it clears itself after HALL_REST_CLEAR_MS of good
            // readings at rest).
            bool faultClearsAtRest() const { return current.fault.kind == FaultKind::HallRail && hallRailAtRest; }
            // Spec 027: a Hall rail fault restored after a restart that was
            // latched at rest (its record says so) clears itself the same way.
            void restoreRestRail() { hallRailAtRest = current.fault.kind == FaultKind::HallRail; }

            // Spec 030 (constitution 8.0.0, I), RoofControllerBlind.cpp: the
            // blind close. Only the automatic close's, only with a Hall rail
            // fault latched (the sensor is dead), roof.blindClose on and none
            // run yet for this fault. After the dead time it drives towards
            // closed at the slow duty on time alone: no magnet, no ramp. It
            // stops at unknownMoveTimeoutMs() (not a fault), on a stall (the
            // supply sagging blindSagVolts below its rest for BLIND_STALL_MS
            // after BLIND_SURGE_MS), on Stop or Abort, and on every fault the
            // controller still checks (supply, late step, raiseFault). The Hall
            // rail fault stays latched; afterwards the state is Error again.
            CommandResult blindClose(Origin origin);
            // A blind close may start now (Status::blindReady).
            bool blindCloseReady() const;
            const BlindReport &blindReport() const { return blindLast; }
            // A blind close has run for the latched fault (kept in its record).
            bool blindDone() const { return blindRan; }
            // After a restart: the record says one ran for this Hall rail fault.
            void restoreBlindDone() { blindRan = current.fault.kind == FaultKind::HallRail; }

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
                uint32_t leftStartMs = 0;     // the step it was last seen leaving it
                uint32_t driveStartMs = 0;
                uint32_t timeoutMs = 0;
                // Spec 016: a calibration leg, and what it has applied so far.
                bool calibration = false;
                CalibrationLeg leg;
                HallLevel startLevel = HallLevel::Unsettled;
                float dutyMs = 0.0f;
                // Spec 019: a jog (its keep-alive is jogHoldPending and jogHoldUntilMs).
                bool jog = false;
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
            bool checkMagnets(uint32_t nowMs);
            bool checkLegMagnets();
            void startLeg(bool leavesOtherEnd, bool onMagnet);
            uint32_t budgetLimitMs() const;
            CalibrationLeg jogLeg(End target) const;
            bool jogExpired(uint32_t nowMs);
            bool jogRunning(uint32_t id) const;
            bool hallBlind() const;
            void endLeg(bool arrived);
            bool checkTimes(uint32_t nowMs);
            bool checkElectrical(const Readings &readings);
            bool confirmed(Pending &pending, bool condition, uint32_t nowMs) const;
            void driveProfile(uint32_t nowMs);
            void advanceEstimate(uint32_t elapsedMs);
            void accountDrive(uint32_t gapMs);
            void arrive();

            // Spec 030: the blind close under way (RoofControllerBlind.cpp).
            struct Blind
            {
                bool active = false;
                bool driving = false;
                uint32_t driveStartMs = 0;
                uint32_t limitMs = 0;
                float restVolts = 0.0f;
                float deepestSag = 0.0f;
                Pending sag; // the sag at or past the threshold
                End target = End::Closed;
                bool jog = false; // spec 033: a person's blind jog, not the blind close
            };
            CommandResult blindReadiness() const;
            void startBlind(End target, uint32_t limitMs);
            void stepBlind(const Readings &readings);
            bool blindStalled(const Readings &readings, uint32_t drivenMs);
            void endBlind(BlindEnd why, const Fault &fault = Fault{});
            void noteMove(uint32_t nowMs);

            // Commands, idle and the latch (RoofController.cpp).
            CommandResult requestMove(End target, Origin origin);
            CommandResult readinessRefusal() const;
            void stopMove(bool keepEstimate);
            void stopDriving();
            void latch(const Fault &fault);
            void noteHall(uint16_t counts);
            void clearRestRail(uint32_t nowMs);
            void checkHallRail(uint16_t counts, uint32_t nowMs);
            void followHall();
            void settleIdle();
            bool magnetEnd(End &end) const;
            float remainingMm() const;
            void resetPending();

            Settings config;
            bool settingsValid;
            HallFilter hall;
            HallTrail recentHall;
            HallTrail faultTrail;
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
            uint32_t faultsLatched = 0;
            // Spec 027: the Hall rail fault latched at rest, and good readings since.
            bool hallRailAtRest = false;
            bool bootSeen = false; // spec 027: the first step's time, for HALL_BOOT_SETTLE_MS
            uint32_t bootMs = 0;
            Pending goodAtRest;
            uint32_t jogCount = 0;
            // Spec 019: renewed since the last step (stamped by the next), and
            // when the jog under way stops unless renewed.
            bool jogHoldPending = false;
            uint32_t jogHoldUntilMs = 0;
            Blind blind;
            BlindReport blindLast;
            bool blindRan = false;
        };
    } // namespace Roof
} // namespace SQM
