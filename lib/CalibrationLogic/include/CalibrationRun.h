#pragma once

// Spec 016: the attended calibration run (research R1-R4, R8). A pure state
// machine that asks the roof controller for one leg at a time
// (Roof::Controller::calibrate) and watches it end: from where the roof is to
// the closed end, a slow open and close, and optionally an open and close at
// the top duty, with a pause at each end. It refuses to start, and ends,
// exactly where spec 016 says; it never moves the roof itself, and it only
// ever stops it (Origin::Automatic, which no close sequence counts as a
// person's) when an opening leg must stop. RoofControl and the demo's roof
// step it right after the controller's tick, under the same lock.

#include <cstdint>
#include <string>

#include "CalibrationProposal.h"
#include "CalibrationStats.h"
#include "RoofController.h"
#include "RoofGates.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace Calib
    {
        enum class Where : uint8_t
        {
            Closed,
            Open,
            PartWay,
        };
        enum class RunState : uint8_t
        {
            Idle, // no run since start-up
            Running,
            Done,
            Failed,  // a fault, a refused leg, or blocked too long
            Stopped, // a stop, the weather, the park, or the automatic close
        };
        enum class Phase : uint8_t
        {
            Settle, // at an end before the next leg (or the end of the run)
            Wait,   // the next leg waits for something that blocks it
            Drive,  // a leg is under way
        };
        enum class Ending : uint8_t
        {
            None,
            Done,
            Stopped,
            Weather, // the weather (or daylight) stopped being safe for opening
            Park,
            CloseSequence,
            Fault,
            Refused,
            Busy,
        };

        const char *whereName(Where where); // "closed", "open", "partWay"
        bool parseWhere(const char *name, Where &out);
        const char *runStateName(RunState state);
        const char *phaseName(Phase phase);
        const char *endingName(Ending ending);

        // The pause at each end, longer than the dead time: the Hall reading at rest.
        constexpr uint32_t SETTLE_MS = 1000;
        // The longest the next leg waits for a blocker (an MQTT connect, a Wi-Fi change).
        constexpr uint32_t LEG_WAIT_MS = 15000;
        // Later legs' limits from the measured ones (research R3).
        constexpr double SLOW_CLOSE_FACTOR = 1.5;
        constexpr double TOP_LEG_FACTOR = 1.25;
        constexpr uint32_t LEG_EXTRA_MS = 5000;
        // A top-duty leg starts slowing down after this fraction of the time the
        // slow leg predicts at the top duty, less the ramp-down time (R4).
        constexpr double SLOW_AFTER_FRACTION = 0.5;
        // The run's limit setting (roof.calibrationLimitSeconds).
        constexpr uint32_t MIN_LIMIT_MS = 30000;
        constexpr uint32_t MAX_LIMIT_MS = 600000;
        constexpr uint32_t MAX_PLANNED_LEGS = 5;

        struct Request
        {
            bool attended = false; // the person confirmed they are at the roof
            Where at = Where::Closed;
            uint32_t travelMm = 0;
            bool topPass = false;
        };

        // What the start is checked against, besides the controller.
        struct StartInputs
        {
            uint32_t nowMs = 0;
            uint32_t limitMs = 0;      // roof.calibrationLimitSeconds in ms
            bool sequenceIdle = true;  // spec 005's sequence is idle
            bool closePending = false; // a close waits for an upload or MQTT connect
            bool settingsPending = false;
            RoofIo::GateInputs gates; // as a command's checks see them now
            bool busy = false;        // something blocks an open (RoofIo::blockerDecision)
            std::string busyReason;   // "busy", "hotspot", "restarting"
            std::string busyText;
            std::string faultText; // the latched fault, worded
        };

        // One control step's view, after the controller's tick.
        struct StepInputs
        {
            Roof::Readings readings; // as the controller got them (current scaled back)
            RoofIo::GateInputs gates;
            bool busy = false; // probed only when wantsLeg() said so
            std::string busyText;
            std::string faultText;
        };

        struct Answer
        {
            bool accepted = true;
            std::string reason;
            std::string text;
        };

        // Everything the status document shows: small enough to copy under the roof's lock.
        struct Report
        {
            RunState state = RunState::Idle;
            Phase phase = Phase::Settle;
            Request request;
            uint32_t limitMs = 0;
            uint8_t legIndex = 0; // 0-based, into the plan
            uint8_t legTotal = 0;
            LegKind leg = LegKind::ToClosed;
            uint32_t phaseStartMs = 0; // when the current pause (or leg) began
            uint32_t legLimitMs = 0;
            uint32_t nowMs = 0; // the last step
            Ending ending = Ending::None;
            std::string endingText;
            Measurements measured;
            Roof::Settings old; // the settings at the start
            bool proposed = false;
            Proposal proposal;
        };

        class Run
        {
        public:
            Answer start(Roof::Controller &controller, const Request &request, const StartInputs &in);
            // The pause is over and a leg is due: the caller probes its blockers for step().
            bool wantsLeg(uint32_t nowMs) const;
            void step(Roof::Controller &controller, const StepInputs &in);
            // An accepted stop or abort from anyone (the roof is being stopped): the run ends.
            void stopped(const std::string &text);
            // The automatic close takes over: the run ends; a closing leg is left
            // to the close, an opening leg is stopped (Origin::Automatic).
            void yieldToClose(Roof::Controller &controller);
            bool active() const { return rep.state == RunState::Running; }
            const Report &report() const { return rep; }

        private:
            Answer startRefusal(const Roof::Controller &controller, const Request &request, const StartInputs &in) const;
            Answer roofRefusal(const Roof::Controller &controller, const Request &request, const StartInputs &in) const;
            void plan(const Roof::Controller &controller, const Request &request);
            Roof::CalibrationLeg legFor(LegKind kind) const;
            const MeasuredLeg *measuredLeg(LegKind kind) const;
            void stepDrive(Roof::Controller &controller, const StepInputs &in);
            void stepPause(Roof::Controller &controller, const StepInputs &in);
            void sample(const Roof::Status &status, const StepInputs &in);
            void legEnded(const Roof::CalibrationReport &report, uint32_t nowMs);
            void startLeg(Roof::Controller &controller, const RoofIo::GateInputs &gates, uint32_t nowMs);
            void pause(uint32_t nowMs);
            void finish(Ending ending, const std::string &text);
            void finishDone();

            Report rep;
            LegKind legs[MAX_PLANNED_LEGS] = {};
            uint8_t legCount = 0;
            uint32_t legsEnded = 0;  // the controller's count when the current leg started
            uint32_t faultsSeen = 0; // the controller's fault count at the start
            LegSamples samples;
            Electrical electrical;
            bool restAtEnd = false; // the pause is at a magnet: its readings are the end's at rest
            Roof::End restEnd = Roof::End::Closed;
            uint32_t waitStartMs = 0;
            Ending endAfterLeg = Ending::None; // a closing leg finishes, then the run ends with this
            std::string endAfterText;
        };
    } // namespace Calib
} // namespace SQM
