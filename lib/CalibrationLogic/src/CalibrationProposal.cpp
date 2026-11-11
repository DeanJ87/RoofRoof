#include "CalibrationProposal.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Config.h"
#include "MotionProfile.h"

// Spec 016: the proposal (research R5 speed and margin, R7 thresholds).

namespace SQM
{
    namespace Calib
    {
        namespace
        {
            std::string format(const char *pattern, ...)
            {
                char text[192];
                va_list args;
                va_start(args, pattern);
                std::vsnprintf(text, sizeof(text), pattern, args);
                va_end(args);
                return text;
            }

            const char *endText(Roof::End end)
            {
                return end == Roof::End::Open ? "open" : "closed";
            }

            double disagreement(double a, double b)
            {
                const double mean = (a + b) / 2.0;
                return mean > 0.0 ? std::fabs(a - b) / mean : 0.0;
            }

            // The measured legs of one duty: slow (false) or top (true).
            uint8_t pairOf(const Measurements &m, bool top, const MeasuredLeg *out[2])
            {
                uint8_t n = 0;
                for (uint8_t i = 0; i < m.legCount && n < 2; ++i)
                {
                    const bool isTop = m.legs[i].kind == LegKind::TopOpen || m.legs[i].kind == LegKind::TopClose;
                    if (isTop == top)
                        out[n++] = &m.legs[i];
                }
                return n;
            }

            // Why the speed can't be trusted ("" when it can), with a warning for a mild disagreement.
            Problem checkLegs(const Measurements &m, std::vector<Problem> &warnings)
            {
                for (uint8_t i = 0; i < m.legCount; ++i)
                    if (m.legs[i].drivenMs < MIN_MEASURED_LEG_MS || !(m.legs[i].dutyMs > 0.0f))
                        return Problem{
                            "legTooShort",
                            format(
                                "The %s took %.1f s, too short for a full travel (at least %.0f s)",
                                legName(m.legs[i].kind),
                                m.legs[i].drivenMs / 1000.0,
                                MIN_MEASURED_LEG_MS / 1000.0)};
                for (bool top : {false, true})
                {
                    const MeasuredLeg *pair[2] = {nullptr, nullptr};
                    if (pairOf(m, top, pair) < 2)
                        continue;
                    const double a = legSpeed(*pair[0], m.travelMm);
                    const double b = legSpeed(*pair[1], m.travelMm);
                    const double d = disagreement(a, b);
                    const std::string text = format(
                        "The %s and %s imply %.1f and %.1f mm/s (%.0f %% apart)",
                        legName(pair[0]->kind),
                        legName(pair[1]->kind),
                        a,
                        b,
                        d * 100.0);
                    if (d > MAX_LEG_DISAGREEMENT)
                        return Problem{
                            "legsDisagree",
                            text +
                                format(", more than %.0f %%: something slipped or a magnet was read early", MAX_LEG_DISAGREEMENT * 100.0)};
                    if (d > WARN_LEG_DISAGREEMENT)
                        warnings.push_back(Problem{"directionsDiffer", text});
                }
                return Problem{};
            }

            // The most a measured leg took longer than `speed` predicts for it.
            double shortfallMs(const Measurements &m, double speed)
            {
                double worst = 0.0;
                for (uint8_t i = 0; i < m.legCount; ++i)
                {
                    const MeasuredLeg &leg = m.legs[i];
                    const double predicted = leg.drivenMs * legSpeed(leg, m.travelMm) / speed;
                    worst = std::max(worst, leg.drivenMs - predicted);
                }
                return worst;
            }

            uint32_t marginSeconds(const Measurements &m, const Roof::Settings &old, float speed)
            {
                Roof::Settings s = old;
                s.speedMmPerSecond = speed;
                s.travelMm = m.travelMm;
                const double move = Roof::expectedMoveMs(s, static_cast<float>(m.travelMm));
                double ms = std::max<double>(MIN_PROPOSED_MARGIN_MS, MARGIN_MOVE_FRACTION * move);
                ms = std::max(ms, MARGIN_SHORTFALL_FACTOR * shortfallMs(m, speed) + MARGIN_SHORTFALL_EXTRA_MS);
                const uint32_t seconds = static_cast<uint32_t>(std::ceil(ms / 1000.0));
                return std::min(RoofConfig::MAX_TIMEOUT_MARGIN_SECONDS, std::max(RoofConfig::MIN_TIMEOUT_MARGIN_SECONDS, seconds));
            }

            void proposeSpeed(const Measurements &m, const Roof::Settings &old, Proposal &p)
            {
                p.speedProblem = checkLegs(m, p.warnings);
                if (!p.speedProblem.reason.empty())
                    return;
                const MeasuredLeg *chosen[2] = {nullptr, nullptr};
                uint8_t n = pairOf(m, true, chosen);
                if (n == 0)
                    n = pairOf(m, false, chosen);
                if (n == 0)
                {
                    p.speedProblem = Problem{"legTooShort", "No full travel was measured"};
                    return;
                }
                double sum = 0.0;
                for (uint8_t i = 0; i < n; ++i)
                    sum += legSpeed(*chosen[i], m.travelMm);
                const float speed = static_cast<float>(std::round(sum / n * 10.0) / 10.0);
                if (speed < RoofConfig::MIN_SPEED_MM_PER_SECOND || speed > RoofConfig::MAX_SPEED_MM_PER_SECOND)
                {
                    p.speedProblem = Problem{
                        "outOfRange",
                        format(
                            "The measured speed %.1f mm/s is outside %.0f-%.0f mm/s",
                            speed,
                            RoofConfig::MIN_SPEED_MM_PER_SECOND,
                            RoofConfig::MAX_SPEED_MM_PER_SECOND)};
                    return;
                }
                p.speed = true;
                p.speedMmPerSecond = speed;
                p.timeoutMarginSeconds = marginSeconds(m, old, speed);
            }

            struct Window
            {
                double lo;
                double hi;
            };

            // The thresholds' range, clipped to RoofConfig's.
            Window clipped(Window w)
            {
                return Window{std::max<double>(w.lo, RoofConfig::MIN_HALL_COUNTS), std::min<double>(w.hi, RoofConfig::MAX_HALL_COUNTS)};
            }

            uint16_t clampInto(uint16_t old, Window w)
            {
                const double lo = std::ceil(w.lo);
                const double hi = std::floor(w.hi);
                return static_cast<uint16_t>(std::min(hi, std::max(lo, static_cast<double>(old))));
            }

            double restMargin(const Stats &rest)
            {
                return std::max(MIN_REST_MARGIN_COUNTS, REST_MARGIN_SIGMAS * rest.sigma());
            }

            Problem samePole(const Measurements &m, bool high)
            {
                return Problem{
                    "samePole",
                    format(
                        "Both ends read %s (closed %.0f, open %.0f counts; in between %.0f): the magnets face the sensor with the same "
                        "pole",
                        high ? "high" : "low",
                        m.closedRest.mean(),
                        m.openRest.mean(),
                        m.between.mean())};
            }

            // An end too close to the in-between level, or "" when both are clear.
            Problem weakEnd(const Measurements &m)
            {
                const double between = m.between.mean();
                for (Roof::End end : {Roof::End::Closed, Roof::End::Open})
                {
                    const Stats &rest = end == Roof::End::Closed ? m.closedRest : m.openRest;
                    const double separation = std::fabs(rest.mean() - between);
                    if (separation < MIN_END_SEPARATION_COUNTS)
                        return Problem{
                            "weakEnd",
                            format(
                                "The %s end reads %.0f counts, %.0f from the in-between level %.0f (at least %.0f needed): the magnet is "
                                "too weak or too far from the sensor",
                                endText(end),
                                rest.mean(),
                                separation,
                                between,
                                MIN_END_SEPARATION_COUNTS)};
                }
                return Problem{};
            }

            Problem noRoom(const char *level, Window w)
            {
                return Problem{
                    "noRoom",
                    format(
                        "No %s threshold is both clear of the in-between readings and read at rest at its end (it would need %.0f to %.0f "
                        "counts)",
                        level,
                        w.lo,
                        w.hi)};
            }

            void proposeHall(const Measurements &m, const Roof::Settings &old, Proposal &p)
            {
                if (m.between.empty() || m.closedRest.empty() || m.openRest.empty())
                {
                    p.hallProblem = Problem{"noRoom", "The Hall readings at both ends and in between weren't all taken"};
                    return;
                }
                const double between = m.between.mean();
                const bool closedHigh = m.closedRest.mean() > between;
                if (closedHigh == (m.openRest.mean() > between))
                {
                    p.hallProblem = samePole(m, closedHigh);
                    return;
                }
                const Roof::End highEnd = closedHigh ? Roof::End::Closed : Roof::End::Open;
                p.hallProblem = weakEnd(m);
                if (!p.hallProblem.reason.empty())
                    return;
                const Stats &highRest = closedHigh ? m.closedRest : m.openRest;
                const Stats &lowRest = closedHigh ? m.openRest : m.closedRest;
                const double clearance = std::max(MIN_CLEARANCE_COUNTS, CLEARANCE_SIGMAS * m.between.sigma());
                const Window high = clipped(Window{m.between.max + clearance, highRest.min - restMargin(highRest)});
                const Window low = clipped(Window{lowRest.max + restMargin(lowRest), m.between.min - clearance});
                if (std::ceil(high.lo) > std::floor(high.hi))
                    return void(p.hallProblem = noRoom("high", high));
                if (std::ceil(low.lo) > std::floor(low.hi))
                    return void(p.hallProblem = noRoom("low", low));
                p.hall = true;
                p.hallHighEnd = highEnd;
                p.hallHighCounts = clampInto(old.hallHighCounts, high);
                p.hallLowCounts = clampInto(old.hallLowCounts, low);
            }

            void warnCurrent(const Measurements &m, Proposal &p)
            {
                if (!m.currentSense || !(m.maxCurrentAmps > 0.0f))
                    return;
                float peak = 0.0f;
                for (uint8_t i = 0; i < m.legCount; ++i)
                    peak = std::max(peak, m.legs[i].electrical.ampsPeak);
                if (peak >= CURRENT_WARN_FRACTION * m.maxCurrentAmps)
                    p.warnings.push_back(Problem{
                        "currentNearLimit",
                        format(
                            "The running current peaked at %.1f A, %.0f %% of the %.1f A limit",
                            peak,
                            peak * 100.0f / m.maxCurrentAmps,
                            m.maxCurrentAmps)});
            }
        } // namespace

        const char *legName(LegKind kind)
        {
            static const char *const NAMES[] = {"toClosed", "slowOpen", "slowClose", "topOpen", "topClose"};
            const unsigned index = static_cast<unsigned>(kind);
            return index < 5 ? NAMES[index] : "unknown";
        }

        float legSpeed(const MeasuredLeg &leg, uint32_t travelMm)
        {
            if (!(leg.dutyMs > 0.0f))
                return 0.0f;
            return static_cast<float>(travelMm / (leg.dutyMs / 1000.0));
        }

        Proposal propose(const Measurements &measured, const Roof::Settings &old)
        {
            Proposal p;
            p.travelMm = measured.travelMm;
            proposeSpeed(measured, old, p);
            proposeHall(measured, old, p);
            warnCurrent(measured, p);
            return p;
        }
    } // namespace Calib
} // namespace SQM
