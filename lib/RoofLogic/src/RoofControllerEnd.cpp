#include "RoofController.h"

#include "MotionProfile.h"

// Spec 036 (constitution 8.1.0, I): an end magnet is believed only where the
// estimate says the roof can be. A stray field, a shifted magnet or a threshold
// taught too close to the reading in between can make the target's level appear
// part-way; taken as an arrival, the roof would report open (or closed) while
// it is not. Read early, the move goes on at the slow duty toward that end and
// accepts the magnet only once the reading has lasted and the estimate reaches
// the plausible point or the reading is clearly at the magnet; else the
// EndTooEarly fault stops it with the position unknown.

namespace SQM
{
    namespace Roof
    {
        // How far toward the target the estimate is, in percent of the travel.
        float Controller::progressPercent() const
        {
            const float travel = static_cast<float>(config.travelMm);
            return 100.0f * (travel - remainingMm()) / travel;
        }

        bool Controller::endPlausible() const
        {
            return progressPercent() >= static_cast<float>(config.endPlausiblePercent);
        }

        // Past the threshold by half the gap between the two thresholds.
        bool Controller::clearlyAtMagnet() const
        {
            const int high = config.hallHighCounts;
            const int low = config.hallLowCounts;
            const int margin = (high - low) / 2;
            const int counts = current.hallCounts;
            if (current.hall == HallLevel::High)
                return counts >= high + margin;
            if (current.hall == HallLevel::Low)
                return counts <= low - margin;
            return false;
        }

        // The target's magnet is read: true when the move ended (arrived, or
        // the EndTooEarly fault); false while an early reading is held.
        bool Controller::checkArrival(uint32_t nowMs)
        {
            if (move.fromUnknown || (!move.early && endPlausible()))
            {
                noteEnd(
                    move.fromUnknown ? EndCheck::Unchecked : EndCheck::Plausible,
                    move.fromUnknown ? 0.0f : progressPercent(),
                    current.hallCounts,
                    0);
                arrive();
                return true;
            }
            if (!move.early)
            {
                move.early = true; // driveProfile drops to the slow duty from this step
                move.earlySinceMs = nowMs;
                move.earlyProgress = progressPercent();
                move.earlyCounts = current.hallCounts;
                return false;
            }
            if (!clearlyAtMagnet())
                move.clearSeen = false;
            else if (!move.clearSeen)
            {
                move.clearSeen = true;
                move.clearSinceMs = nowMs;
            }
            const bool clear = move.clearSeen && nowMs - move.clearSinceMs >= EARLY_END_PERSIST_MS;
            const uint32_t heldMs = nowMs - move.earlySinceMs;
            if (heldMs >= EARLY_END_PERSIST_MS && (clear || endPlausible()))
            {
                noteEnd(EndCheck::Late, move.earlyProgress, move.earlyCounts, heldMs);
                arrive();
                return true;
            }
            if (heldMs < EARLY_END_HOLD_MS)
                return false;
            endTooEarly(nowMs);
            return true;
        }

        void Controller::endTooEarly(uint32_t nowMs)
        {
            noteEnd(EndCheck::TooEarly, move.earlyProgress, move.earlyCounts, nowMs - move.earlySinceMs);
            latch(Fault{FaultKind::EndTooEarly, move.earlyProgress, static_cast<float>(config.endPlausiblePercent)});
        }

        // A move ends while an early reading is held (its fault, any other
        // fault, a stop or a reversal): that end's magnet isn't believed and
        // the position is unknown.
        void Controller::abandonEarly()
        {
            if (!move.active || !move.early)
                return;
            doubting = true;
            doubtedEnd = move.target;
            current.positionKnown = false;
        }

        void Controller::noteEnd(EndCheck result, float progress, uint16_t counts, uint32_t heldMs)
        {
            endLast.ended++;
            endLast.result = result;
            endLast.target = move.target;
            endLast.progressPercent = progress;
            endLast.counts = counts;
            endLast.heldMs = heldMs;
        }
    } // namespace Roof
} // namespace SQM
