#include "MoveLog.h"

#include <algorithm>

#include "RoofSettings.h"

// The recorder (research R1, R3, R5, R6). Everything is reset in place: no
// temporary Move on the control task's stack.

namespace SQM
{
    namespace Diag
    {
        namespace
        {
            float lowest(float now, float value)
            {
                return std::isnan(now) ? value : std::min(now, value);
            }

            float highest(float now, float value)
            {
                return std::isnan(now) ? value : std::max(now, value);
            }

            // Pairs of point values: a missing side takes the other side's.
            uint16_t mergeAverage(uint16_t a, uint16_t b)
            {
                if (a == NONE)
                    return b;
                if (b == NONE)
                    return a;
                return static_cast<uint16_t>((static_cast<uint32_t>(a) + b + 1) / 2);
            }

            uint16_t mergeHighest(uint16_t a, uint16_t b)
            {
                if (a == NONE)
                    return b;
                if (b == NONE)
                    return a;
                return std::max(a, b);
            }

            uint16_t mergeLowest(uint16_t a, uint16_t b)
            {
                if (a == NONE)
                    return b;
                if (b == NONE)
                    return a;
                return std::min(a, b);
            }

            bool arrived(Roof::End target, Roof::State state)
            {
                return (target == Roof::End::Open && state == Roof::State::Open) ||
                       (target == Roof::End::Closed && state == Roof::State::Closed);
            }
        } // namespace

        void MoveLog::step(const Sample &sample)
        {
            const Roof::State state = sample.status.state;
            if (!primed)
            {
                // Attached part-way through a move: that move isn't recorded.
                primed = true;
                updateRest(sample);
                updatePhase(sample);
                lastState = state;
                lastMs = sample.nowMs;
                return;
            }
            const bool changed = state != lastState;
            if (recording && (!moving(state) || changed))
                endMove(sample);
            if (moving(state) && changed)
                startMove(sample);
            if (recording)
                recordStep(sample);
            updateRest(sample);
            updatePhase(sample);
            lastState = state;
            lastMs = sample.nowMs;
        }

        void MoveLog::startMove(const Sample &sample)
        {
            newest = held == 0 ? 0 : static_cast<uint8_t>((newest + 1) % MAX_MOVES);
            if (held < MAX_MOVES)
                ++held;
            Move &move = current();
            move.summary = Summary();
            move.intervalMs = FIRST_INTERVAL_MS;
            move.count = 0;
            Summary &s = move.summary;
            s.id = nextMoveId;
            nextMoveId = nextMoveId == UINT32_MAX ? 1 : nextMoveId + 1;
            s.startMs = sample.nowMs;
            s.endMs = sample.nowMs;
            s.origin = sample.origin;
            s.target = sample.status.target;
            s.fromUnknown = sample.status.fromUnknown;
            s.hallStart = sample.status.hallCounts;
            s.hallEnd = sample.status.hallCounts;
            s.endState = sample.status.state;
            s.active = true;
            acc = Accumulator();
            drivenAmpsSum = 0.0;
            drivenAmpsCount = 0;
            drivenYet = false;
            stallCount = 0;
            stallNext = 0;
            lastDrivenDuty = 0;
            reachedTop = false;
            slowingSeen = false;
            recording = true;
        }

        void MoveLog::recordStep(const Sample &sample)
        {
            Summary &s = current().summary;
            s.endMs = sample.nowMs;
            s.hallEnd = sample.status.hallCounts;
            s.endState = sample.status.state;
            if (sample.status.driving)
            {
                if (!drivenYet)
                {
                    drivenYet = true;
                    s.restVolts = liveState.restVolts;
                }
                s.drivenMs += sample.nowMs - lastMs;
                recordDriven(sample, s);
            }
            advancePoints(sample.nowMs - s.startMs, sample.output.duty);
            accumulate(sample);
        }

        void MoveLog::recordDriven(const Sample &sample, Summary &s)
        {
            if (std::isfinite(sample.motorAmps))
            {
                s.peakAmps = highest(s.peakAmps, sample.motorAmps);
                drivenAmpsSum += sample.motorAmps;
                ++drivenAmpsCount;
                stall[stallNext] = sample.motorAmps;
                stallNext = static_cast<uint8_t>((stallNext + 1) % STALL_READINGS);
                if (stallCount < STALL_READINGS)
                    ++stallCount;
            }
            if (Roof::supplyReadable(sample.supplyVolts))
                s.minVolts = lowest(s.minVolts, sample.supplyVolts);
        }

        float MoveLog::stallAverage() const
        {
            if (stallCount == 0)
                return NAN;
            float sum = 0.0f;
            for (uint8_t i = 0; i < stallCount; ++i)
                sum += stall[i];
            return sum / static_cast<float>(stallCount);
        }

        void MoveLog::endMove(const Sample &sample)
        {
            if (acc.steps > 0)
                advancePoints(UINT32_MAX, sample.output.duty); // flush only: see below
            Summary &s = current().summary;
            s.endMs = sample.nowMs;
            s.endState = sample.status.state;
            s.hallEnd = sample.status.hallCounts;
            s.active = false;
            if (drivenAmpsCount > 0)
                s.averageAmps = static_cast<float>(drivenAmpsSum / static_cast<double>(drivenAmpsCount));
            const Roof::Fault &fault = sample.status.fault;
            s.fault = fault.kind;
            if (fault.kind != Roof::FaultKind::None)
            {
                s.result = Result::Fault;
                s.faultMeasured = fault.measured;
                s.faultLimit = fault.limit;
                if (drivenYet)
                    s.stallAmps = stallAverage();
            }
            else
            {
                s.result = arrived(s.target, s.endState) ? Result::Arrived : Result::Stopped;
            }
            recording = false;
            ++finishedMoves;
        }

        // Closes every interval that has ended by `elapsedMs`; an interval with
        // no step (a late step) gets an empty point. UINT32_MAX flushes only
        // the point being filled (the move's end).
        void MoveLog::advancePoints(uint32_t elapsedMs, uint8_t duty)
        {
            if (elapsedMs == UINT32_MAX)
            {
                Point point;
                point.ampsAvg = acc.ampsCount > 0 ? toHundredths(acc.ampsSum / static_cast<float>(acc.ampsCount)) : NONE;
                point.ampsMax = toHundredths(acc.ampsMax);
                point.voltsMin = toHundredths(acc.voltsMin);
                point.duty = static_cast<uint8_t>((acc.dutySum + acc.steps / 2) / acc.steps);
                acc = Accumulator();
                pushPoint(point);
                return;
            }
            Move &move = current();
            while (static_cast<uint64_t>(elapsedMs) >= static_cast<uint64_t>(move.count + 1) * move.intervalMs)
            {
                if (acc.steps > 0)
                {
                    advancePoints(UINT32_MAX, duty);
                    continue;
                }
                Point empty;
                empty.duty = duty;
                pushPoint(empty);
            }
        }

        void MoveLog::accumulate(const Sample &sample)
        {
            ++acc.steps;
            acc.dutySum += sample.output.duty;
            if (std::isfinite(sample.motorAmps))
            {
                acc.ampsSum += sample.motorAmps;
                ++acc.ampsCount;
                acc.ampsMax = highest(acc.ampsMax, sample.motorAmps);
            }
            if (Roof::supplyReadable(sample.supplyVolts))
                acc.voltsMin = lowest(acc.voltsMin, sample.supplyVolts);
        }

        void MoveLog::pushPoint(const Point &point)
        {
            Move &move = current();
            move.points[move.count++] = point;
            if (move.count == MAX_POINTS)
                mergePoints();
        }

        // Points 2i and 2i+1 become point i; the interval doubles. The point
        // being filled next starts at 60 x 2T = 120 x T: on a boundary.
        void MoveLog::mergePoints()
        {
            Move &move = current();
            for (uint8_t i = 0; i < MAX_POINTS / 2; ++i)
            {
                const Point &a = move.points[2 * i];
                const Point &b = move.points[2 * i + 1];
                Point merged;
                merged.ampsAvg = mergeAverage(a.ampsAvg, b.ampsAvg);
                merged.ampsMax = mergeHighest(a.ampsMax, b.ampsMax);
                merged.voltsMin = mergeLowest(a.voltsMin, b.voltsMin);
                merged.duty = static_cast<uint8_t>((a.duty + b.duty + 1) / 2);
                move.points[i] = merged;
            }
            move.count = MAX_POINTS / 2;
            move.intervalMs *= 2;
        }

        // The supply at rest: blocks of REST_BLOCK_MS with the motor off. A
        // driven step starts the block again.
        void MoveLog::updateRest(const Sample &sample)
        {
            if (sample.status.driving)
            {
                restSum = 0.0f;
                restCount = 0;
                return;
            }
            if (!Roof::supplyReadable(sample.supplyVolts))
                return;
            if (restCount == 0)
                restStartMs = sample.nowMs;
            restSum += sample.supplyVolts;
            ++restCount;
            if (sample.nowMs - restStartMs >= REST_BLOCK_MS)
            {
                liveState.restVolts = restSum / static_cast<float>(restCount);
                restSum = 0.0f;
                restCount = 0;
            }
        }

        // While driven from a known position: the profile's rise, top and fall.
        Phase MoveLog::drivenPhase(const Sample &sample)
        {
            const uint8_t duty = sample.output.duty;
            if (duty >= sample.topDuty)
            {
                reachedTop = true;
                return Phase::Cruise;
            }
            if (reachedTop || (lastDrivenDuty > 0 && duty < lastDrivenDuty))
                slowingSeen = true;
            if (!slowingSeen)
                return Phase::SoftStart;
            return duty <= sample.slowDuty ? Phase::Creep : Phase::Slowing;
        }

        void MoveLog::updatePhase(const Sample &sample)
        {
            const Roof::Status &st = sample.status;
            Phase phase = Phase::Idle;
            if (moving(st.state))
            {
                if (!st.driving)
                    phase = Phase::DeadTime;
                else
                    phase = st.fromUnknown ? Phase::Slow : drivenPhase(sample);
            }
            if (st.driving)
                lastDrivenDuty = sample.output.duty;
            liveState.phase = phase;
            const bool sagKnown = st.driving && Roof::supplyReadable(sample.supplyVolts);
            liveState.sagVolts = sagKnown ? liveState.restVolts - sample.supplyVolts : NAN;
            liveState.moveActive = recording;
            liveState.moveId = held > 0 ? current().summary.id : 0;
        }

        void MoveLog::clear()
        {
            held = 0;
            newest = 0;
            recording = false;
            acc = Accumulator();
            liveState.moveId = 0;
            liveState.moveActive = false;
        }

        bool MoveLog::copy(uint32_t id, Move &out) const
        {
            for (uint8_t k = 0; k < held; ++k)
            {
                const Move &move = moves[(newest + MAX_MOVES - k) % MAX_MOVES];
                if (move.summary.id == id)
                {
                    out = move;
                    return true;
                }
            }
            return false;
        }

        uint8_t MoveLog::ids(uint32_t (&out)[MAX_MOVES]) const
        {
            for (uint8_t k = 0; k < held; ++k)
                out[k] = moves[(newest + MAX_MOVES - k) % MAX_MOVES].summary.id;
            return held;
        }

        bool MoveLog::latest(Summary &out) const
        {
            if (held == 0)
                return false;
            out = current().summary;
            return true;
        }

        LiveLog MoveLog::live() const
        {
            return liveState;
        }
    } // namespace Diag
} // namespace SQM
