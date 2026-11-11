#pragma once

// Command-line OTA's marker check (spec 011 FR-024, research R2). ArduinoOTA
// gives no access to the image while it is written, so the image is checked
// after Update.end() has made it the boot partition and before the restart:
// if this board wouldn't take it over the web upload (another product, another
// layout, or a bench image on a standard board), the running firmware stays
// the boot partition.

#include <string>

namespace SQM
{
    namespace OtaImageCheck
    {
        // True if the new image was refused and the boot partition put back;
        // `why` is FirmwareImage::verdictMessage's text. False when the image
        // is fine (or nothing new was written).
        bool revertIfRefused(std::string &why);
    } // namespace OtaImageCheck
} // namespace SQM
