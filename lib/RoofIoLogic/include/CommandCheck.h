#pragma once

// Whether a roof command may run now, before anything changes (spec 003's
// gates, spec 006's client rule, spec 002's own checks). RoofControl and the
// Alpaca simulator both decide with this, so the command path ConformU
// exercises is the firmware's (constitution IV).

#include <string>

#include "RoofController.h"
#include "RoofGates.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        struct CommandCheck
        {
            bool accepted = true;
            std::string reason; // machine name: a gate's, "closing", or the motion logic's refusal
            std::string text;   // why, with the value and the limit or the input that failed
        };

        // Stop and abort: always accepted (constitution I). Open and close:
        // the weather and park gates (constitution II, III), then, for an
        // Alpaca client, no open while the roof is closing or a close waits
        // (a client never reverses a person's or the automatic close), then
        // the motion logic's dry run (position unknown, fault, supply...).
        CommandCheck checkCommand(
            Roof::Command command, Roof::Origin origin, const GateInputs &gates, const Roof::Controller &controller, bool closePending);
    } // namespace RoofIo
} // namespace SQM
