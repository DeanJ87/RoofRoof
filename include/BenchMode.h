#pragma once

// Bench mode (spec 011, contracts/bench-mode.md): a separate firmware build
// (`pio run -e esp32dev_bench`) for a bare board on the desk. The roof is
// simulated (lib/RoofPlantLogic) and the motor driver's pins are never driven
// (Driver::physicalOutputs). The standard build has no way into bench mode: no
// setting, command or document turns it on, and it refuses bench images over
// the network (lib/FirmwareImage). The names match FirmwareImage::BUILD and
// FirmwareImage::BUILD_BENCH (test_firmware_image checks).

#ifndef ROOFROOF_BENCH
#define ROOFROOF_BENCH 0
#endif

#if ROOFROOF_BENCH
#define ROOFROOF_BUILD_NAME "bench"
#else
#define ROOFROOF_BUILD_NAME "standard"
#endif

namespace SQM
{
    namespace Bench
    {
        constexpr bool BUILD = ROOFROOF_BENCH != 0;
        constexpr const char *BUILD_NAME = ROOFROOF_BUILD_NAME;
        // The status document's roof.simulation for this build.
        constexpr const char *SIMULATION = BUILD ? "bench" : "off";
    } // namespace Bench
} // namespace SQM
