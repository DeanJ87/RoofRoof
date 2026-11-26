#pragma once

// Spec 047 (constitution 12.0.0, II): a move that started with the park
// confirmed goes on to its end when N.I.N.A. merely stops answering (the
// network down, N.I.N.A. unreachable, a read timing out): "a command is a
// command", and a roof left part-open is the worst place for it (spec 043:
// the telescope may move only with the roof at an end). This tracks one move:
// when it began running on a stale park reading (logged), and when it reached
// its end that way (logged, and an information alert). Pure (constitution V).

#include <cstdint>
#include <string>

#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        enum class StaleStep : uint8_t
        {
            None,
            Began,    // the move under way now goes on without a fresh park reading
            Finished, // a move that did reached the end it was going to
        };

        class StaleRide
        {
        public:
            // Every control step: the roof as it is, and whether the move under
            // way goes on only on a stale park reading (RoofIo::movesOnStale).
            // A move stopped short (Stop, a fault, the interlock) is forgotten
            // without a report: its own stop says what happened.
            StaleStep step(const Roof::Status &status, bool onStale);
            // The end the move was going to (valid after Began or Finished).
            Roof::End target() const { return end; }
            // The move under way has gone on without a fresh park reading.
            bool riding() const { return rode; }

        private:
            bool rode = false;
            Roof::End end = Roof::End::Closed;
        };

        // "Close continues without a fresh park reading (N.I.N.A. unreachable):
        // it started with the park confirmed", "Finished the close without a
        // fresh park reading (N.I.N.A. unreachable)"; "" for None.
        std::string staleRideText(StaleStep step, Roof::End target);
    } // namespace RoofIo
} // namespace SQM
