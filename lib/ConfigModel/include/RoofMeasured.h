#pragma once

// Spec 026: the bits of RoofConfig::measured: which roof values were measured on the
// roof (an accepted calibration run, a confirmed teach, the wizard's direction answer)
// and not typed since. A save that changes one without sending `measured` clears its bit.

#include <cstdint>

namespace SQM
{
    namespace Measured
    {
        constexpr uint8_t TRAVEL = 1;
        constexpr uint8_t SPEED = 2;
        constexpr uint8_t MARGIN = 4;
        constexpr uint8_t HALL = 8; // the high end and both thresholds
        constexpr uint8_t DIRECTION = 16;
        constexpr uint8_t ALL = 31;
    } // namespace Measured
} // namespace SQM
