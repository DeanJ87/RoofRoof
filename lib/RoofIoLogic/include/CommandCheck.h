#pragma once

// Whether a roof command may run now, before anything changes (spec 003's
// gates, spec 006's client rule, spec 002's own checks). RoofControl and the
// Alpaca simulator both decide with this, so the command path ConformU
// exercises is the firmware's (constitution IV).

#include <cstdint>
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
            // Spec 014: an accepted remote stop that counts as a person's (the
            // owner's autoClose.remoteStop setting): the automatic close stands
            // down and raises its critical alert, as for a person's stop.
            bool standsDown = false;
        };

        // Spec 052 (finding F39): the automatic close is armed (spec 030's weather
        // hold has fired and the weather hasn't yet been safe for the hold's
        // length): an Open now would be closed again at once. `leftMs`: the safe
        // spell still needed before it stands down; `holdMs`: the hold's length.
        struct CloseArmed
        {
            bool armed = false;
            uint32_t leftMs = 0;
            uint32_t holdMs = 0;
        };

        // What else the decision needs from the roof's owner (RoofControl).
        struct CommandContext
        {
            bool closePending = false; // a close is waiting for an upload or MQTT connect to stop
            // Spec 014 (autoClose.remoteStop): a remote client's stop of a close
            // in weather that isn't safe (or in daylight) is accepted and counts
            // as a person's stop, instead of being refused. Off by default.
            bool remoteStopsClose = false;
            // Spec 016: a calibration run is under way. Open and close are
            // refused (`calibrating`) for every origin but the automatic close,
            // which ends the run first (RoofControl::automaticClose).
            bool calibrating = false;
            // Spec 027: what a person confirmed past (RoofIo::OverrideBit). Only
            // a person's count: Open takes weather, daylight and (spec 039) the
            // park, Close the park.
            uint8_t overrides = 0;
            // Spec 027: the park set aside whoever asks (MoveGuard's "refused
            // only for the park" dry run; never used to run a command).
            bool parkAside = false;
            // Spec 052: the automatic close is armed (CloseArmed). A remote Open
            // is refused (CLOSE_ARMED_REASON) while the weather reads safe; a
            // person's isn't (finding F39b, left to the owner).
            CloseArmed closeArmed;
        };

        // Spec 027: the overrides `origin` may use for `command` (0 for clients
        // and MQTT; spec 030: the automatic close's Close only OverrideNinaLost;
        // spec 039: a person's Open the park, weather and daylight).
        uint8_t allowedOverrides(Roof::Command command, Roof::Origin origin, uint8_t requested);

        constexpr const char *CALIBRATING_REASON = "calibrating";
        // Spec 052: a remote Open while the automatic close is armed.
        constexpr const char *CLOSE_ARMED_REASON = "closeArmed";
        // "The automatic close is still armed after unsafe weather and would close
        // the roof again at once: an imaging app can't open it until the weather
        // has been safe for 60 s (42 s left)".
        std::string closeArmedText(Roof::Origin origin, const CloseArmed &armed);
        // Spec 019: open and close from anyone but the automatic close while a
        // person jogs the roof (the automatic close stops the jog first).
        constexpr const char *JOGGING_REASON = "jogging";

        // An origin with no login that isn't the controller: an Alpaca client
        // (spec 006) or MQTT (spec 008). Each gets every rule below that
        // names a client; none is weaker than another.
        bool remoteOrigin(Roof::Origin origin);

        // Stop and abort: accepted, except a remote client's while a close is
        // under way or waiting and the weather isn't safe, whoever started the
        // close (constitution III: nothing unauthenticated may call off or
        // delay the roof closing in bad weather; a person's stop from the web
        // UI and the controller's own stops still work). With the context's
        // remoteStopsClose such a stop is accepted with standsDown, so it is
        // never silent (spec 014). Open and close: the weather and park gates
        // (constitution II, III); then, for a remote client, no open while the roof
        // is closing, a close waits, or a close was stopped part-way (a client
        // never reverses a close); then the motion logic's dry run (position
        // unknown, fault, supply...). Spec 052: a remote Open is also refused
        // while the automatic close is armed (before the park gate, so it never
        // waits for a park it couldn't use). Spec 027: an Open or Close of a roof
        // already at that end (nothing moving) is accepted first, whatever the
        // checks say: there is nothing to do.
        CommandCheck checkCommand(
            Roof::Command command,
            Roof::Origin origin,
            const GateInputs &gates,
            const Roof::Controller &controller,
            const CommandContext &context);
    } // namespace RoofIo
} // namespace SQM
