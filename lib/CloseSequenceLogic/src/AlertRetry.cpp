#include "AlertRetry.h"

namespace SQM
{
    namespace CloseSeq
    {
        void AlertRetry::sent(uint64_t nowMs)
        {
            waiting = true;
            lastSendMs = nowMs;
        }

        bool AlertRetry::resendDue(Delivery delivery, uint64_t nowMs)
        {
            if (!waiting)
                return false;
            if (delivery == Delivery::Delivered || delivery == Delivery::NoChannel)
            {
                waiting = false;
                return false;
            }
            if (delivery == Delivery::Pending)
                return false;
            return nowMs > lastSendMs && nowMs - lastSendMs >= ALERT_RETRY_MS;
        }
    } // namespace CloseSeq
} // namespace SQM
