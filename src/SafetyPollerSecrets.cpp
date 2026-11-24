#include "Logger.h"
#include "SafetyPoller.h"

// Spec 024: SQMeter's secrets (GET /api/secrets) for the sync, read in the
// poller pass after a good settings read; kept under the lock, never in the
// snapshot. The decisions are lib/SafetyInputLogic's (SqmeterSecrets).

namespace SQM
{
    Site::SecretsRead SafetyPoller::secretsRead() const
    {
        if (mutex == nullptr)
            return Site::SecretsRead{};
        xSemaphoreTake(mutex, portMAX_DELAY);
        Site::SecretsRead copy = secrets;
        xSemaphoreGive(mutex);
        return copy;
    }

    Site::SecretsProblem SafetyPoller::secretsProblem() const
    {
        if (mutex == nullptr)
            return Site::SecretsProblem::Waiting;
        xSemaphoreTake(mutex, portMAX_DELAY);
        const Site::SecretsProblem problem = secrets.problem;
        xSemaphoreGive(mutex);
        return problem;
    }

    void SafetyPoller::pollSecrets(const Site::Endpoint &from, uint32_t generation, uint64_t basedOnMs)
    {
        // SQMeter's settings say sharing is off: nothing to ask (an empty URL).
        if (from.url.empty())
        {
            Site::SecretsRead off = Site::sharingOffRead(nowMs());
            off.basedOnMs = basedOnMs;
            xSemaphoreTake(mutex, portMAX_DELAY);
            if (generation == state.siteGeneration)
                secrets = std::move(off);
            xSemaphoreGive(mutex);
            return;
        }
        DynamicJsonDocument filter(Site::SECRETS_FILTER_BYTES);
        Site::secretsFilter(filter);
        DynamicJsonDocument doc(Site::SECRETS_DOC_BYTES);
        const Site::Fetch fetch = fetchJson(from, filter, doc, FetchLimits{LOCATION_CONNECT_MS, LOCATION_TOTAL_MS});
        Site::SecretsRead fresh = Site::readSecrets(fetch, doc, nowMs());
        fresh.basedOnMs = basedOnMs;
        if (!fresh.good())
            Logger::debug("SafetyPoller", "SQMeter secrets not read: %s", Site::secretsProblemName(fresh.problem));
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.siteGeneration)
            secrets = std::move(fresh);
        xSemaphoreGive(mutex);
    }
} // namespace SQM
