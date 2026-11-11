#pragma once

// The move recorder (spec 017 FR-008 to FR-013, FR-020 to FR-022; research
// R1, R3, R5, R6). Stepped by the control task once per step, under the roof's
// lock, after the step has applied its output. step() allocates nothing,
// waits for nothing and does no I/O: constant work per step, plus one merge
// of MAX_POINTS points when a profile fills (research R1). Readers copy under
// the same lock.

#include <cstdint>

#include "DiagTypes.h"

namespace SQM
{
    namespace Diag
    {
        // The log's part of the live document.
        struct LiveLog
        {
            Phase phase = Phase::Idle;
            float restVolts = NAN; // the latest full rest block's average
            float sagVolts = NAN;  // rest - now while driven
            uint32_t moveId = 0;   // the move in progress or the last one; 0: none
            bool moveActive = false;
        };

        class MoveLog
        {
        public:
            // One control step. The first step after construction only primes:
            // a move already under way isn't recorded (FR-008).
            void step(const Sample &sample);

            // Drops every held move, the one in progress included (the
            // firmware refuses this while the roof moves). Ids continue.
            void clear();
            // The next move's id (1 when given 0): continues after kept summaries.
            void setNextId(uint32_t id) { nextMoveId = id == 0 ? 1 : id; }
            uint32_t nextId() const { return nextMoveId; }

            // Moves finished since the log was created (the main loop's cue).
            uint32_t finished() const { return finishedMoves; }
            // A held move by id; false when it isn't held.
            bool copy(uint32_t id, Move &out) const;
            // The held moves' ids, newest first; returns how many.
            uint8_t ids(uint32_t (&out)[MAX_MOVES]) const;
            // The move in progress or the last one; false when none.
            bool latest(Summary &out) const;
            LiveLog live() const;

        private:
            // The point being filled.
            struct Accumulator
            {
                float ampsSum = 0.0f;
                uint16_t ampsCount = 0;
                float ampsMax = NAN;
                float voltsMin = NAN;
                uint32_t dutySum = 0;
                uint16_t steps = 0;
            };

            void startMove(const Sample &sample);
            void recordStep(const Sample &sample);
            void recordDriven(const Sample &sample, Summary &summary);
            void endMove(const Sample &sample);
            void advancePoints(uint32_t elapsedMs, uint8_t duty);
            void accumulate(const Sample &sample);
            void pushPoint(const Point &point);
            void mergePoints();
            void updateRest(const Sample &sample);
            void updatePhase(const Sample &sample);
            Phase drivenPhase(const Sample &sample);
            float stallAverage() const;
            Move &current() { return moves[newest]; }
            const Move &current() const { return moves[newest]; }

            Move moves[MAX_MOVES];
            uint8_t newest = 0;
            uint8_t held = 0;
            bool recording = false;
            bool primed = false;
            Roof::State lastState = Roof::State::Unknown;
            uint32_t lastMs = 0;
            uint32_t nextMoveId = 1;
            uint32_t finishedMoves = 0;

            // The move in progress.
            Accumulator acc;
            double drivenAmpsSum = 0.0;
            uint32_t drivenAmpsCount = 0;
            bool drivenYet = false;
            float stall[STALL_READINGS] = {};
            uint8_t stallCount = 0;
            uint8_t stallNext = 0;
            uint8_t lastDrivenDuty = 0;
            bool reachedTop = false;
            bool slowingSeen = false;

            // Across moves.
            float restSum = 0.0f;
            uint16_t restCount = 0;
            uint32_t restStartMs = 0;
            LiveLog liveState;
        };
    } // namespace Diag
} // namespace SQM
