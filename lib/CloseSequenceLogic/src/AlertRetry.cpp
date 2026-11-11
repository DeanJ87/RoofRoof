#include "AlertRetry.h"

namespace SQM
{
    namespace CloseSeq
    {
        void AlertRetry::sent(uint64_t nowMs)
        {
            waiting = true;
            lastSendMs = nowMs;
            gap = ALERT_RETRY_MS;
        }

        void AlertRetry::resent(uint64_t nowMs)
        {
            waiting = true;
            lastSendMs = nowMs;
            gap = gap * 2 > ALERT_RETRY_MAX_MS ? ALERT_RETRY_MAX_MS : gap * 2;
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
            const uint64_t since = nowMs > lastSendMs ? nowMs - lastSendMs : 0;
            // A delivery that never reports back counts as failed.
            if (delivery == Delivery::Pending && since < ALERT_PENDING_STALE_MS)
                return false;
            return since >= gap;
        }
    } // namespace CloseSeq
} // namespace SQM
