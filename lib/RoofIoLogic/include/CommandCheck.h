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

        // What else the decision needs from the roof's owner (RoofControl).
        struct CommandContext
        {
            bool closePending = false; // a close is waiting for an upload or MQTT connect to stop
        };

        // Stop and abort: accepted, except an Alpaca client's while a close is
        // under way or waiting and the weather isn't safe, whoever started the
        // close (constitution III: nothing unauthenticated may call off or
        // delay the roof closing in bad weather; a person's stop from the web
        // UI and the controller's own stops still work). Open and close: the weather and park gates
        // (constitution II, III); then, for a client, no open while the roof
        // is closing, a close waits, or a close was stopped part-way (a client
        // never reverses a close); then the motion logic's dry run (position
        // unknown, fault, supply...).
        CommandCheck checkCommand(
            Roof::Command command,
            Roof::Origin origin,
            const GateInputs &gates,
            const Roof::Controller &controller,
            const CommandContext &context);
    } // namespace RoofIo
} // namespace SQM
