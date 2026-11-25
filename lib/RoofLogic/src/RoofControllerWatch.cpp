#include "RoofController.h"

#include <algorithm>
#include <cstdlib>

#include "MotionProfile.h"

// Spec 036 (constitution 9.1.0, I): two signs that a move is not doing what the
// estimate says, each a motion fault before the hard timeout.
// - No end magnet where expected: a move from a known position that has driven
//   roof.endOverrunPercent of its expected time (at least END_OVERRUN_MIN_MS
//   more) without its target magnet. By then the profile is at the slow duty
//   (the estimate has arrived), so the roof isn't pushed into its end stop at
//   speed until the timeout (a shifted magnet in the field).
// - No movement: driving without a supply sag of roof.motionSagVolts (held
//   CONFIRM_MS, after the start-up surge) and without a Hall change of
//   MOTION_HALL_COUNTS within roof.motionCheckMs (twice that for a calibration
//   leg or a jog). The Hall reading is flat between the magnets, so the sag is
//   the usual sign; a slow duty too low to move the roof shows neither.

namespace SQM
{
    namespace Roof
    {
        void Controller::startWatch(const Readings &readings)
        {
            move.restVolts = readings.supplyVolts; // this step's reading was taken with the driver off
            move.watchSinceMs = readings.nowMs;
            move.watchHall = readings.hallCounts;
            move.deepestSag = 0.0f;
            move.sagging = Pending{};
            move.overrunMs = 0;
            if (move.fromUnknown || move.calibration || config.endOverrunPercent == 0)
                return;
            const uint32_t expectedMs = expectedMoveMs(config, remainingMm());
            const uint64_t scaled = static_cast<uint64_t>(expectedMs) * config.endOverrunPercent / 100;
            const uint64_t floor = static_cast<uint64_t>(expectedMs) + END_OVERRUN_MIN_MS;
            move.overrunMs = static_cast<uint32_t>(std::min<uint64_t>(std::max(scaled, floor), UINT32_MAX));
        }

        // `>=` the limit; not while an early end reading is held (it has its own bound).
        bool Controller::checkOverrun(uint32_t elapsedMs)
        {
            if (move.overrunMs == 0 || move.early || elapsedMs < move.overrunMs)
                return false;
            latch(Fault{FaultKind::NoEndMagnet, static_cast<float>(elapsedMs), static_cast<float>(move.overrunMs)});
            return true;
        }

        bool Controller::checkMovement(const Readings &readings)
        {
            if (config.motionSagVolts <= 0.0f)
                return false;
            const uint32_t nowMs = readings.nowMs;
            if (nowMs - move.driveStartMs < BLIND_SURGE_MS)
            {
                move.watchSinceMs = nowMs;
                move.watchHall = readings.hallCounts;
                return false;
            }
            const bool readable = supplyReadable(readings.supplyVolts) && supplyReadable(move.restVolts);
            const float sag = readable ? move.restVolts - readings.supplyVolts : 0.0f;
            move.deepestSag = std::max(move.deepestSag, sag);
            const bool sagged = confirmed(move.sagging, sag >= config.motionSagVolts, nowMs);
            const bool hallMoved = std::abs(static_cast<int>(readings.hallCounts) - move.watchHall) >= MOTION_HALL_COUNTS;
            if (sagged || hallMoved)
            {
                move.watchSinceMs = nowMs;
                move.watchHall = readings.hallCounts;
                move.deepestSag = 0.0f;
                return false;
            }
            const uint32_t windowMs = move.calibration ? 2 * config.motionCheckMs : config.motionCheckMs;
            if (nowMs - move.watchSinceMs < windowMs)
                return false;
            latch(Fault{FaultKind::NoMovement, move.deepestSag, config.motionSagVolts});
            return true;
        }
    } // namespace Roof
} // namespace SQM
