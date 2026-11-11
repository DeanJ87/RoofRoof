#include "ResetReason.h"

namespace SQM
{
    const char *resetReasonText(esp_reset_reason_t reason)
    {
        switch (reason)
        {
        case ESP_RST_POWERON:
            return "power-on";
        case ESP_RST_BROWNOUT:
            return "brown-out";
        case ESP_RST_SW:
            return "software restart";
        case ESP_RST_PANIC:
            return "crash (panic)";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return "task watchdog";
        case ESP_RST_EXT:
            return "reset pin";
        case ESP_RST_DEEPSLEEP:
            return "deep sleep";
        default:
            return "other";
        }
    }
} // namespace SQM
