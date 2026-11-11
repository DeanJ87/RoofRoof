#pragma once

#include <esp_system.h>

namespace SQM
{
    // Why the device last started, in words ("power-on", "brown-out"...).
    const char *resetReasonText(esp_reset_reason_t reason);
} // namespace SQM
