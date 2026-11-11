#pragma once

#include "FirmwareImage.h"

#include <string>

namespace SQM
{
    // This firmware's own marker (lib/FirmwareImage, SQMeter spec 027 FR-020)
    // and the partition layout it is running on.
    namespace FirmwareMarker
    {
        // The marker's text after the magic, "product=roofroof;layout=l2;build=standard;".
        const char *fields();
        // "l2", or "other" for any other partition table.
        const std::string &layout();
    } // namespace FirmwareMarker
} // namespace SQM
