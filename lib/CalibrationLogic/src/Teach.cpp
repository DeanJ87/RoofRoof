#include "Teach.h"

#include "MotionGuards.h"
#include "RoofSettings.h"
#include "RoofText.h"

// Spec 019: the jog's start and renewal, the reading window, the marks and
// the proposal. The document is in TeachDocument.cpp.

namespace SQM
{
    namespace Calib
    {
        namespace
        {
            Answer refuse(const char *reason, const std::string &text)
            {
                return Answer{false, reason, text};
            }

            bool railCounts(uint16_t counts)
            {
                return counts <= Roof::HALL_RAIL_LOW_COUNTS || counts >= Roof::HALL_RAIL_HIGH_COUNTS;
            }
        } // namespace

        TeachProposal proposeTeach(const Stats &closed, const Stats &open, const Stats &partWay)
        {
            TeachProposal p;
            p.made = true;
            HallFit fit;
            p.problem = fitHall(closed, open, partWay, fit);
            if (!p.problem.reason.empty())
                return p;
            const Stats &high = fit.highEnd == Roof::End::Closed ? closed : open;
            const Stats &low = fit.highEnd == Roof::End::Closed ? open : closed;
            const double mid = partWay.mean();
            p.ok = true;
            p.highEnd = fit.highEnd;
            p.highCounts = clampCounts((mid + high.mean()) / 2.0, fit.highLo, fit.highHi);
            p.lowCounts = clampCounts((mid + low.mean()) / 2.0, fit.lowLo, fit.lowHi);
            return p;
        }

        Answer Teach::jog(Roof::Controller &controller, Roof::End target, bool attended, bool calibrating, const StartInputs &in)
        {
            Answer common = attendedRefusal(controller, attended, calibrating, in);
            if (!common.accepted)
                return common;
            const Roof::CommandResult result = controller.jog(target, Roof::Origin::Person);
            if (!result.accepted)
                return refuse(Roof::refusalName(result.reason), RoofIo::describeRefusal(result));
            return Answer{};
        }

        Answer Teach::hold(Roof::Controller &controller, uint32_t id)
        {
            if (controller.holdJog(id))
                return Answer{};
            return refuse("jogEnded", "The jog has ended: press and hold the button again");
        }

        Answer Teach::release(Roof::Controller &controller, uint32_t id)
        {
            if (controller.releaseJog(id))
                return Answer{};
            return refuse("jogEnded", "The jog has already ended");
        }

        bool Teach::yieldToClose(Roof::Controller &controller)
        {
            if (!controller.status().jogging)
                return false;
            controller.command(Roof::Command::Stop, Roof::Origin::Automatic);
            return true;
        }

        // At rest: no move, no jog, no leg, the driver off, the reading off the
        // rails. Anything else empties the window.
        void Teach::step(const Roof::Status &status, uint16_t counts)
        {
            const bool rest = !status.driving && !status.calibrating && !RoofIo::roofMoving(status.state);
            if (!rest || railCounts(counts))
            {
                filled = 0;
                next = 0;
                return;
            }
            ring[next] = counts;
            next = static_cast<uint8_t>((next + 1) % REST_READINGS);
            if (filled < REST_READINGS)
                ++filled;
        }

        Stats Teach::window() const
        {
            Stats s;
            for (uint8_t i = 0; i < filled; ++i)
                s.add(ring[i]);
            return s;
        }

        Answer Teach::mark(Where where, const Roof::Status &status, bool calibrating)
        {
            if (calibrating)
                return refuse("calibrating", "A calibration run is under way");
            if (filled < REST_READINGS || status.jogging || RoofIo::roofMoving(status.state))
                return refuse("notAtRest", "Wait until the roof has been still for a second");
            if (status.hall == Roof::HallLevel::Unsettled || status.hall == Roof::HallLevel::Rail)
                return refuse("hallNotSettled", "The Hall reading isn't settled or is at a rail: check the sensor");
            const unsigned index = static_cast<unsigned>(where);
            if (index >= MARK_COUNT)
                return refuse("invalid", "Say closed, open or part-way");
            marks[index] = window();
            marked[index] = true;
            return Answer{};
        }

        TeachProposal Teach::proposal() const
        {
            for (bool m : marked)
                if (!m)
                    return TeachProposal{};
            return proposeTeach(marks[0], marks[1], marks[2]);
        }

        Answer Teach::checkConfirm(Roof::End highEnd, uint16_t highCounts, uint16_t lowCounts) const
        {
            const TeachProposal p = proposal();
            if (!p.made)
                return refuse("noProposal", "Nothing to confirm: mark the closed end, the open end and part-way first");
            if (!p.ok)
                return refuse("noProposal", p.problem.text);
            if (p.highEnd != highEnd || p.highCounts != highCounts || p.lowCounts != lowCounts)
                return refuse("proposalChanged", "The proposal has changed since it was shown: check it again");
            return Answer{};
        }

        void Teach::clear()
        {
            for (uint8_t i = 0; i < MARK_COUNT; ++i)
            {
                marks[i] = Stats{};
                marked[i] = false;
            }
        }

        TeachReport Teach::report(const Roof::Controller &controller) const
        {
            TeachReport r;
            const Roof::Status &status = controller.status();
            r.jogActive = status.jogging;
            r.jogId = controller.jogId();
            r.jogTarget = status.target;
            r.jogElapsedMs = status.jogging ? status.moveElapsedMs : 0;
            r.jogLimitMs = controller.jogLimitMs();
            r.restReady = filled == REST_READINGS;
            r.restMean = r.restReady ? window().mean() : 0.0;
            for (uint8_t i = 0; i < MARK_COUNT; ++i)
            {
                r.marked[i] = marked[i];
                r.marks[i] = marks[i];
            }
            r.proposal = proposal();
            return r;
        }
    } // namespace Calib
} // namespace SQM
