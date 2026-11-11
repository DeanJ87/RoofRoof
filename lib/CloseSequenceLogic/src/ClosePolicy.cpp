#include "ClosePolicy.h"

#include "Config.h"

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            uint64_t within(uint64_t ms, const PolicyRange &range)
            {
                return ms < range.minMs ? range.minMs : ms > range.maxMs ? range.maxMs : ms;
            }
        } // namespace

        Policy clamp(const Policy &policy)
        {
            Policy out = policy;
            out.parkRequestGapMs = within(policy.parkRequestGapMs, PARK_REQUEST_GAP_RANGE);
            out.parkSettleMs = within(policy.parkSettleMs, PARK_SETTLE_RANGE);
            out.retryAfterMs = within(policy.retryAfterMs, RETRY_AFTER_RANGE);
            out.rearmSafeMs = within(policy.rearmSafeMs, REARM_SAFE_RANGE);
            out.closeStartMs = within(policy.closeStartMs, CLOSE_START_RANGE);
            out.closeGuardSlackMs = within(policy.closeGuardSlackMs, CLOSE_GUARD_SLACK_RANGE);
            out.remindMs = within(policy.remindMs, REMIND_RANGE);
            return out;
        }

        Policy policyFrom(const Config &config)
        {
            const AutoCloseConfig &a = config.autoClose;
            Policy p;
            p.parkRequestGapMs = static_cast<uint64_t>(a.parkRequestSeconds) * 1000u;
            p.parkSettleMs = static_cast<uint64_t>(a.parkHoldSeconds) * 1000u;
            p.retryAfterMs = static_cast<uint64_t>(a.retryMinutes) * 60000u;
            p.rearmSafeMs = static_cast<uint64_t>(a.rearmMinutes) * 60000u;
            p.closeStartMs = static_cast<uint64_t>(a.closeStartSeconds) * 1000u;
            p.closeGuardSlackMs = static_cast<uint64_t>(a.closeGuardSeconds) * 1000u;
            p.remindMs = static_cast<uint64_t>(a.remindMinutes) * 60000u;
            p.retryNotStarted = a.notStarted == AutoCloseConfig::NotStarted::Retry;
            return clamp(p);
        }
    } // namespace CloseSeq
} // namespace SQM
