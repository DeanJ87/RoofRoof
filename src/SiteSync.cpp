#include "SiteSync.h"

#include "DeviceCore.h"
#include "Logger.h"
#include "PlatformGuard.h"
#include <memory>
#include <time.h>

// Spec 020: the location and the followed settings read from SQMeter, saved
// as flash writes (spec 010: never while the roof moves or a close waits;
// tried again next time) under the lock every settings save holds.

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Site";

        uint8_t followMask(const SqmeterSyncConfig &s)
        {
            return static_cast<uint8_t>((s.time.follow ? 1 : 0) | (s.mqtt.follow ? 2 : 0) | (s.alerts.follow ? 4 : 0));
        }
    } // namespace

    SiteSync::SiteSync(Config &config, std::mutex &saveLock, SafetyPoller &poller, std::function<void(bool)> applied)
        : config(config),
          saveLock(saveLock),
          poller(poller),
          applied(std::move(applied))
    {
    }

    void SiteSync::handle(bool busy)
    {
        poller.setRoofBusy(busy);
        const uint32_t now = millis();
        if (busy || now - lastCheckMs < 1000)
            return;
        lastCheckMs = now;
        // The reads are taken under the lock, so they belong to the settings saved now.
        std::unique_lock<std::mutex> held(saveLock, std::try_to_lock);
        if (!held.owns_lock())
            return;
        const time_t epoch = time(nullptr);
        uint32_t generation = 0;
        const Site::LocationRead location = poller.locationRead(generation);
        if (location.good() && location.atMs != locationAtMs && saveLocation(location, epoch))
            locationAtMs = location.atMs;
        // A follow switch just turned on syncs from the last read at once.
        const uint8_t follows = followMask(config.sqmeterSync);
        Site::ConfigRead settings = poller.settingsRead();
        // Spec 024: SQMeter's secrets, when they were read for this settings read.
        const Site::SecretsRead secrets = poller.secretsRead();
        Site::pairSecrets(settings.site, settings.goodAtMs, secrets);
        const bool fresh = settings.atMs != settingsAtMs || secrets.atMs != secretsAtMs;
        if (settings.good() && (fresh || follows != syncedFollows) && syncSettings(settings, epoch))
        {
            settingsAtMs = settings.atMs;
            secretsAtMs = secrets.atMs;
            syncedFollows = follows;
        }
    }

    // Saved when Site::locationToSave says so. False when the flash is held.
    bool SiteSync::saveLocation(const Site::LocationRead &read, time_t epoch)
    {
        const auto next = Site::locationToSave(config.location, read, static_cast<int64_t>(epoch), epoch >= Core::CLOCK_VALID_EPOCH);
        if (!next)
            return true;
        const PlatformGuard::FlashWrite write;
        if (write.refused() != RoofIo::HoldReason::None)
            return false;
        const auto updated = std::make_unique<Config>(config);
        updated->location = *next;
        if (!updated->save())
        {
            Logger::error(TAG, "Couldn't save the location read from SQMeter");
            return true;
        }
        config.location = *next;
        applied(true);
        Logger::info(TAG, "Location from SQMeter%s saved: %.4f, %.4f", next->gps ? " (GPS)" : "", next->latitude, next->longitude);
        return true;
    }

    // Each followed group through the settings' own validation (a refused
    // value keeps the old one). False when the flash is held.
    bool SiteSync::syncSettings(const Site::ConfigRead &read, time_t epoch)
    {
        // On the heap: settings copies would crowd the loop task's stack.
        const auto outcome = std::make_unique<Site::SyncOutcome>();
        Site::syncFromSite(*outcome, config, read.site, static_cast<int64_t>(epoch), epoch >= Core::CLOCK_VALID_EPOCH);
        {
            const std::lock_guard<std::mutex> shown(stateLock);
            for (size_t i = 0; i < Site::GROUP_COUNT; ++i)
            {
                const std::string &why = outcome->groups[i].refused;
                if (!why.empty() && why != state.refused[i])
                    Logger::warn(TAG, "SQMeter's %s settings not taken: %s", Site::groupName(static_cast<Site::Group>(i)), why.c_str());
                state.refused[i] = why;
            }
        }
        if (!outcome->any())
            return true;
        const PlatformGuard::FlashWrite write;
        if (write.refused() != RoofIo::HoldReason::None)
            return false;
        if (!outcome->next.save())
        {
            Logger::error(TAG, "Couldn't save the settings synced from SQMeter");
            return true;
        }
        const std::string changed = outcome->changedNames();
        Site::applyOutcome(*outcome, config); // only what changed: the live settings aren't reassigned whole
        applied(changed.empty());
        if (!changed.empty())
            Logger::info(TAG, "Settings synced from SQMeter: %s", changed.c_str());
        return true;
    }

    void SiteSync::writeStatus(JsonDocument &doc) const
    {
        uint32_t generation = 0;
        const Site::LocationRead location = poller.locationRead(generation);
        const bool addressSet = !Site::siteUrl(config.safety.httpUrl, Site::STATUS_PATH).empty();
        Site::writeLocationStatus(doc.createNestedObject("location"), config.location, location, addressSet, SafetyPoller::nowMs());
        Site::SyncState shown;
        {
            const std::lock_guard<std::mutex> held(stateLock);
            shown = state;
        }
        const Site::ConfigRead settings = poller.settingsRead();
        // Only the problem: the status never copies a secret.
        Site::SecretsRead secrets;
        secrets.problem = poller.secretsProblem();
        Site::writeSyncStatus(
            doc.createNestedObject("sync"), Site::SyncView{config, settings, shown, addressSet, SafetyPoller::nowMs(), &secrets});
    }
} // namespace SQM
