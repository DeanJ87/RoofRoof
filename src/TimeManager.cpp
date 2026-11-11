#include "TimeManager.h"
#include "Logger.h"
#include <Arduino.h>
#include <esp_sntp.h>

namespace SQM
{
    namespace
    {
        // A clock below this has never been set (the ESP32 starts in 1970).
        constexpr time_t CLOCK_SET_EPOCH = 1000000000;
        constexpr uint32_t SYNC_TIMEOUT_MS = 30000;
        constexpr uint32_t RETRY_WITHOUT_NTP_MS = 300000; // 5 min
    } // namespace

    TimeManager::TimeManager(const NTPConfig &ntpCfg)
        : ntpConfig(ntpCfg)
    {
    }

    void TimeManager::begin()
    {
        Logger::info(TAG, "Initializing time synchronization");
        configureTimezone();
        if (ntpConfig.enabled)
            startSntp();
        syncStatus = NTPSyncStatus::IN_PROGRESS;
        lastSyncAttempt = millis();
        Logger::info(TAG, "Time synchronization started");
    }

    // esp_sntp_*: the sntp_* names are deprecated in ESP-IDF 5.
    void TimeManager::startSntp()
    {
        Logger::info(TAG, "Configuring NTP: %s / %s", ntpConfig.server1.c_str(), ntpConfig.server2.c_str());
        if (esp_sntp_enabled())
            esp_sntp_stop();
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, ntpConfig.server1.c_str());
        if (!ntpConfig.server2.empty())
            esp_sntp_setservername(1, ntpConfig.server2.c_str());
        esp_sntp_init();
    }

    void TimeManager::handle()
    {
        const uint32_t now = millis();

        // Periodic sync attempts based on interval
        bool needsSync = false;
        if (syncStatus == NTPSyncStatus::SYNCED || syncStatus == NTPSyncStatus::FAILED)
        {
            const uint32_t syncInterval = ntpConfig.enabled ? ntpConfig.syncIntervalMs : RETRY_WITHOUT_NTP_MS;
            needsSync = now - lastSyncAttempt >= syncInterval;
        }

        // A clock that was never set: try again at once.
        time_t currentTime;
        time(&currentTime);
        if (currentTime < CLOCK_SET_EPOCH && syncStatus != NTPSyncStatus::IN_PROGRESS)
            needsSync = true;

        if (needsSync)
            syncTime();

        if (syncStatus != NTPSyncStatus::IN_PROGRESS)
            return;
        time(&currentTime);
        if (currentTime > CLOCK_SET_EPOCH)
        {
            syncStatus = NTPSyncStatus::SYNCED;
            lastSuccessfulSync = now;
            lastKnownTime = currentTime;
            Logger::info(TAG, "Time sync successful via NTP");
        }
        else if (now - lastSyncAttempt > SYNC_TIMEOUT_MS)
        {
            syncStatus = NTPSyncStatus::FAILED;
            Logger::warn(TAG, "Time sync timeout");
        }
    }

    void TimeManager::syncTime()
    {
        lastSyncAttempt = millis();
        if (!ntpConfig.enabled)
        {
            syncStatus = NTPSyncStatus::FAILED;
            return;
        }
        Logger::info(TAG, "Attempting time sync from NTP");
        syncStatus = NTPSyncStatus::IN_PROGRESS;
        // NTP is asynchronous: handle() sees the clock set.
        startSntp();
    }

    void TimeManager::configureTimezone()
    {
        Logger::info(TAG, "Setting timezone: %s", ntpConfig.timezone.c_str());
        setenv("TZ", ntpConfig.timezone.c_str(), 1);
        tzset();
    }

    void TimeManager::updateConfig(const NTPConfig &newNtpConfig)
    {
        const bool serverChanged = ntpConfig.server1 != newNtpConfig.server1 || ntpConfig.server2 != newNtpConfig.server2;
        const bool tzChanged = ntpConfig.timezone != newNtpConfig.timezone;
        const bool enabledChanged = ntpConfig.enabled != newNtpConfig.enabled;
        ntpConfig = newNtpConfig;

        if (tzChanged)
            configureTimezone();
        if (!ntpConfig.enabled && esp_sntp_enabled())
            esp_sntp_stop();
        if ((serverChanged || enabledChanged) && ntpConfig.enabled)
        {
            Logger::info(TAG, "Time config changed, re-synchronizing");
            syncTime();
        }
    }

    // Time until the next NTP sync is due (0 if not synced or overdue).
    uint32_t TimeManager::nextSyncInMs() const
    {
        if (syncStatus != NTPSyncStatus::SYNCED)
            return 0;
        const uint32_t syncInterval = ntpConfig.enabled ? ntpConfig.syncIntervalMs : RETRY_WITHOUT_NTP_MS;
        const uint32_t timeSinceSync = millis() - lastSuccessfulSync;
        return timeSinceSync < syncInterval ? syncInterval - timeSinceSync : 0; // 0: overdue for sync
    }

    TimeStatus TimeManager::getStatus() const
    {
        TimeStatus status;
        status.ntpEnabled = ntpConfig.enabled;
        status.syncStatus = syncStatus;
        status.lastSyncMs = lastSuccessfulSync;
        status.server = ntpConfig.server1;
        status.timezone = ntpConfig.timezone;
        status.nextSyncMs = nextSyncInMs();
        status.driftSeconds = 0;
        return status;
    }

    std::string TimeManager::getCurrentTimeISO() const
    {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
        return std::string(buffer);
    }

    time_t TimeManager::getCurrentTime() const
    {
        time_t now;
        time(&now);
        return now;
    }

} // namespace SQM
