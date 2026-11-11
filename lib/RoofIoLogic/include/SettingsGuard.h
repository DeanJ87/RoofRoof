#pragma once

// Settings that change motion are refused while the roof moves (constitution
// I; spec 003 FR-016, research R9): the roof's own settings and the park
// settings the close interlock depends on.

#include "Config.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // Any roof or park setting differs between `current` and `next`.
        bool motionSettingsChanged(const Config &current, const Config &next);
        // A save is refused when it changes motion settings while opening or closing.
        // Spec 016: and for the whole of a calibration run.
        bool settingsSaveRefused(Roof::State state, bool motionChanged, bool calibrating = false);
    } // namespace RoofIo
} // namespace SQM
