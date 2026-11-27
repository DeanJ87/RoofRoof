#include "AlertSend.h"

namespace SQM
{
    namespace Alerts
    {
        const char *sendWaitText(SendWait wait)
        {
            switch (wait)
            {
            case SendWait::RoofMoving:
                return "Waiting for the roof to stop";
            case SendWait::ShortOfMemory:
                return "Waiting for memory";
            case SendWait::GiveUp:
                return "Waited 10 min: sent again later";
            case SendWait::Go:
                break;
            }
            return "";
        }

        void SendGate::start(uint32_t nowMs)
        {
            startedMs = nowMs;
            retryMs = 0;
            backoffMs = SEND_MEMORY_FIRST_MS;
        }

        SendWait SendGate::check(uint32_t nowMs, bool roofMoving, bool memoryFits)
        {
            if (!roofMoving && memoryFits)
            {
                retryMs = 0;
                return SendWait::Go;
            }
            if (nowMs - startedMs >= SEND_GIVE_UP_MS)
            {
                ++gaveUpCount;
                retryMs = 0;
                return SendWait::GiveUp;
            }
            if (roofMoving)
            {
                ++moveWaitCount;
                retryMs = SEND_MOVE_RECHECK_MS;
                return SendWait::RoofMoving;
            }
            ++memoryWaitCount;
            retryMs = backoffMs;
            backoffMs = backoffMs >= SEND_MEMORY_MAX_MS / 2 ? SEND_MEMORY_MAX_MS : backoffMs * 2;
            return SendWait::ShortOfMemory;
        }
    } // namespace Alerts
} // namespace SQM
