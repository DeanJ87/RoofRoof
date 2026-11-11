#pragma once

// The roof as the Alpaca Dome (spec 006, data-model.md): which shutter state
// the roof is in, which roof command a Dome action is, the text a refusal
// carries, and which stops stand spec 005's automatic close down. The firmware
// and the Alpaca simulator both use these, so neither has its own copy
// (constitution IV).

#include <string>

#include "AlpacaDome.h"
#include "FaultRecord.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // ShutterStatus and Slewing. Only the closed magnet reads closed and
        // only the open magnet open; a waiting close reads closing; stopped
        // part-way, an unknown position, a fault or a controller that isn't
        // running reads error.
        Alpaca::DomeStatus domeStatus(const Roof::Status &status, bool closePending, bool running);

        // OpenShutter -> Open, CloseShutter -> Close, AbortSlew -> Stop (the
        // position estimate is kept).
        Roof::Command roofCommand(Alpaca::DomeAction action);

        // A stop or abort from a person counts as a person's stop: spec 005's
        // sequence stands down on it. An Alpaca client's doesn't: Alpaca has no
        // login, and an unauthenticated stop must not be able to call off the
        // close on unsafe weather (constitution III). It stops the roof all the
        // same; the sequence sees an interrupted close and tries again.
        bool countsAsPersonStop(Roof::Command command, Roof::Origin origin);

        // The Alpaca error text for a refused command: `text`, and for a
        // latched fault (`reason` "faultLatched") the fault with its value and limit.
        std::string domeRefusalText(const std::string &reason, const std::string &text, const FaultRecord &fault);

        // The Alpaca answer to a roof command's outcome. A stop is always
        // accepted: the roof applies every stop it gets, and when the
        // controller isn't running the driver is off already (constitution I:
        // a stop is never refused).
        Alpaca::DomeAnswer domeAnswer(
            Roof::Command command, bool accepted, const std::string &reason, const std::string &text, const FaultRecord &fault);
    } // namespace RoofIo
} // namespace SQM
