#pragma once

// Spec 043 (constitution 11.1.0, II): the telescope may move (any slew, a park
// or an unpark included) only with the roof at one of its ends: at the closed
// magnet or the open magnet, at rest. Part-open, moving, with the position
// unknown or a fault latched, a slew can hit the roof's beams, which is worse
// than rain. RoofRoof never asks N.I.N.A. to park (a slew) unless this holds,
// and it tells N.I.N.A. through the Alpaca Switch (MountSwitches) so a
// sequence can gate its own slews. Pure (constitution V).

#include <cstdint>
#include <string>

#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // Where the roof stands for the telescope's sake.
        enum class RoofEnd : uint8_t
        {
            None,   // part-open, moving, unknown, faulted, or the controller not running
            Closed, // at rest on the closed magnet
            Open,   // at rest on the open magnet
        };

        // The end the roof is at rest on, read from the motion logic's state
        // alone: only State::Closed and State::Open, which the motion logic
        // reports only at a believed magnet (spec 036) and never during a jog
        // or a calibration leg. Anything else is None.
        RoofEnd roofAtEnd(const Roof::Status &status);
        // The same from the state alone (the close sequence sees only the state).
        RoofEnd roofAtEnd(Roof::State state);

        // The telescope may move now: the roof is at an end.
        bool mountMayMove(const Roof::Status &status);
        bool mountMayMove(Roof::State state);

        // RoofRoof may ask N.I.N.A. to park now (the automatic close's request
        // and a command's park wait alike): only while the mount may move. The
        // one predicate both senders check (src/CloseOnUnsafe.cpp, src/main.cpp).
        inline bool parkRequestAllowed(const Roof::Status &status)
        {
            return mountMayMove(status);
        }
        // The same from the state alone, for a sender that holds only the roof's
        // state (RoofControl::state(): Closed and Open only at rest on a magnet).
        inline bool parkRequestAllowed(Roof::State state)
        {
            return mountMayMove(state);
        }

        // Where the roof is, for the texts: "part-open (42 %)", "position
        // unknown", "moving", "faulted". `travelMm` turns the estimate into percent.
        std::string roofPlaceText(const Roof::Status &status, uint32_t travelMm);

        // The needs-person text while the roof isn't at an end and the mount
        // isn't parked: "Roof part-open (42 %) with the mount not parked:
        // moving the roof or the telescope would hit the beams. A person must
        // check."
        std::string mountBlockedText(const Roof::Status &status, uint32_t travelMm);

        // The Alpaca Switch device's read-only switches (spec 043), in their
        // fixed order: the order is the Switch Id N.I.N.A. keeps, so never reorder.
        enum class MountSwitch : uint8_t
        {
            MountMayMove = 0, // the roof is at an end: the telescope may slew, park or unpark
            RoofOpen = 1,     // at rest on the open magnet
            RoofClosed = 2,   // at rest on the closed magnet
        };
        constexpr uint8_t MOUNT_SWITCH_COUNT = 3;

        // Each switch's value now; all false while the controller isn't running.
        bool mountSwitchValue(MountSwitch which, const Roof::Status &status, bool running);
        // Each switch's fixed name and description (Alpaca GetSwitchName, GetSwitchDescription).
        const char *mountSwitchName(MountSwitch which);
        const char *mountSwitchDescription(MountSwitch which);
    } // namespace RoofIo
} // namespace SQM
