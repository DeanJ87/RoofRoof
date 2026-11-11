#include "CalibrationRun.h"

#include <cstring>

#include "Config.h"
#include "MotionGuards.h"
#include "RoofText.h"

// Spec 016: starting and ending a run, and the names. The legs themselves are
// in CalibrationRunLegs.cpp.

namespace SQM
{
    namespace Calib
    {
        namespace
        {
            template <typename E, unsigned N> const char *nameIn(E value, const char *const (&names)[N])
            {
                const unsigned index = static_cast<unsigned>(value);
                return index < N ? names[index] : "unknown";
            }

            Answer refuse(const std::string &reason, const std::string &text)
            {
                return Answer{false, reason, text};
            }

            bool onMagnet(Roof::HallLevel level)
            {
                return level == Roof::HallLevel::High || level == Roof::HallLevel::Low;
            }
        } // namespace

        const char *whereName(Where where)
        {
            static const char *const NAMES[] = {"closed", "open", "partWay"};
            return nameIn(where, NAMES);
        }

        bool parseWhere(const char *name, Where &out)
        {
            if (name == nullptr)
                return false;
            for (Where w : {Where::Closed, Where::Open, Where::PartWay})
                if (std::strcmp(name, whereName(w)) == 0)
                {
                    out = w;
                    return true;
                }
            return false;
        }

        const char *runStateName(RunState state)
        {
            static const char *const NAMES[] = {"idle", "running", "done", "failed", "stopped"};
            return nameIn(state, NAMES);
        }

        const char *phaseName(Phase phase)
        {
            static const char *const NAMES[] = {"settle", "wait", "drive"};
            return nameIn(phase, NAMES);
        }

        const char *endingName(Ending ending)
        {
            static const char *const NAMES[] = {"none", "done", "stopped", "weather", "park", "closeSequence", "fault", "refused", "busy"};
            return nameIn(ending, NAMES);
        }

        // Spec 016 FR-001 to FR-004, in this order: the request, the run, the
        // close sequence and the roof, then the weather, the park and the
        // blockers, then the roof's own readiness (roofRefusal).
        Answer Run::startRefusal(const Roof::Controller &controller, const Request &request, const StartInputs &in) const
        {
            if (!request.attended)
                return refuse("notAttended", "Confirm that you are at the roof and can see it");
            if (active())
                return refuse("calibrating", "A calibration run is already under way");
            if (!in.sequenceIdle)
                return refuse("closeSequence", "The automatic close is busy: calibrate once it is idle");
            const Roof::Status &status = controller.status();
            if (status.fault.kind != Roof::FaultKind::None)
                return refuse("faultLatched", "A fault is latched: " + in.faultText);
            if (RoofIo::roofMoving(status.state))
                return refuse("moving", "The roof is moving");
            if (in.closePending)
                return refuse("closeWaiting", "A close is waiting to start");
            if (in.settingsPending)
                return refuse("settingsPending", "New roof settings are being applied: try again in a moment");
            const RoofIo::GateRefusal open = RoofIo::openRefusal(in.gates);
            if (open != RoofIo::GateRefusal::None)
                return refuse(RoofIo::gateRefusalName(open), RoofIo::describeGate(open, in.gates));
            if (!RoofIo::parkHolds(in.gates))
                return refuse(
                    RoofIo::gateRefusalName(RoofIo::GateRefusal::MountNotParked),
                    RoofIo::describeGate(RoofIo::GateRefusal::MountNotParked, in.gates));
            if (in.busy)
                return refuse(in.busyReason.empty() ? "busy" : in.busyReason, in.busyText);
            return roofRefusal(controller, request, in);
        }

        Answer Run::roofRefusal(const Roof::Controller &controller, const Request &request, const StartInputs &in) const
        {
            const Roof::Settings &s = controller.settings();
            if (request.travelMm < RoofConfig::MIN_TRAVEL_MM || request.travelMm > RoofConfig::MAX_TRAVEL_MM)
                return refuse("invalid", "The travel must be 100-20000 mm");
            if (request.topPass && s.topDutyPercent <= s.slowDutyPercent)
                return refuse("invalid", "The top duty isn't above the slow duty: there is no top-duty pass to run");
            if (in.limitMs < MIN_LIMIT_MS || in.limitMs > MAX_LIMIT_MS)
                return refuse("invalid", "The calibration time limit must be 30-600 s");
            const Roof::HallLevel hall = controller.status().hall;
            if (hall == Roof::HallLevel::Between ? request.at != Where::PartWay : request.at == Where::PartWay && onMagnet(hall))
                return refuse(
                    "hallDisagrees",
                    request.at == Where::PartWay ? "The Hall sensor reads a magnet: the roof is at an end, not part-way"
                                                 : "The Hall sensor reads no magnet: the roof isn't at an end");
            Roof::CalibrationLeg first;
            first.target = request.at == Where::Closed ? Roof::End::Open : Roof::End::Closed;
            first.dutyPercent = s.slowDutyPercent;
            first.limitMs = in.limitMs;
            const Roof::CommandResult check = controller.checkCalibrate(first, Roof::Origin::Person);
            if (!check.accepted)
                return refuse(Roof::refusalName(check.reason), RoofIo::describeRefusal(check));
            return Answer{};
        }

        Answer Run::start(Roof::Controller &controller, const Request &request, const StartInputs &in)
        {
            Answer refused = startRefusal(controller, request, in);
            if (!refused.accepted)
                return refused;
            rep = Report();
            rep.state = RunState::Running;
            rep.request = request;
            rep.limitMs = in.limitMs;
            rep.old = controller.settings();
            rep.measured.travelMm = request.travelMm;
            rep.measured.currentSense = rep.old.currentSenseEnabled;
            rep.measured.maxCurrentAmps = rep.old.maxCurrentAmps;
            rep.nowMs = in.nowMs;
            endAfterLeg = Ending::None;
            endAfterText.clear();
            plan(controller, request);
            legsEnded = controller.calibrationReport().ended;
            pause(in.nowMs);
            return Answer{};
        }

        void Run::plan(const Roof::Controller &controller, const Request &request)
        {
            legCount = 0;
            if (request.at != Where::Closed)
                legs[legCount++] = LegKind::ToClosed;
            legs[legCount++] = LegKind::SlowOpen;
            legs[legCount++] = LegKind::SlowClose;
            if (request.topPass)
            {
                legs[legCount++] = LegKind::TopOpen;
                legs[legCount++] = LegKind::TopClose;
            }
            rep.legTotal = legCount;
            rep.legIndex = 0;
            rep.leg = legs[0];
            const Roof::HallLevel hall = controller.status().hall;
            restAtEnd = request.at != Where::PartWay && onMagnet(hall);
            restEnd = request.at == Where::Open ? Roof::End::Open : Roof::End::Closed;
            if (request.at == Where::Closed)
                rep.measured.closedLevel = hall;
        }

        void Run::stopped(const std::string &text)
        {
            if (active())
                finish(Ending::Stopped, text);
        }

        void Run::yieldToClose()
        {
            if (active())
                finish(Ending::CloseSequence, "The automatic close took over");
        }

        void Run::finish(Ending ending, const std::string &text)
        {
            rep.ending = ending;
            rep.endingText = text;
            switch (ending)
            {
            case Ending::Done:
                rep.state = RunState::Done;
                break;
            case Ending::Fault:
            case Ending::Refused:
            case Ending::Busy:
                rep.state = RunState::Failed;
                break;
            default:
                rep.state = RunState::Stopped;
                break;
            }
        }

        void Run::finishDone()
        {
            rep.proposal = propose(rep.measured, rep.old);
            rep.proposed = true;
            finish(Ending::Done, "Calibration finished");
        }
    } // namespace Calib
} // namespace SQM
