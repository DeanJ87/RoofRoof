#pragma once

#include "Config.h"
#include <cstdint>
#include <string>
#include <sys/time.h>
#include <time.h>

namespace SQM
{
    enum class NTPSyncStatus
    {
        NOT_SYNCED = 0,
        IN_PROGRESS = 1,
        SYNCED = 2,
        FAILED = 3
    };

    struct TimeStatus
    {
        bool ntpEnabled;
        NTPSyncStatus syncStatus;
        uint32_t lastSyncMs;
        uint32_t nextSyncMs;
        int32_t driftSeconds;
        std::string server;
        std::string timezone;
    };

    // The device clock from NTP, and the time zone it reports in. NTP is the
    // only source (no GPS, spec 001 research R11).
    class TimeManager
    {
    public:
        explicit TimeManager(const NTPConfig &ntpCfg);
        ~TimeManager() = default;

        void begin();
        void handle();
        void updateConfig(const NTPConfig &newNtpConfig);

        TimeStatus getStatus() const;
        std::string getCurrentTimeISO() const;
        time_t getCurrentTime() const;

    private:
        uint32_t nextSyncInMs() const;
        static constexpr const char *TAG = "TimeManager";

        NTPConfig ntpConfig;

        NTPSyncStatus syncStatus = NTPSyncStatus::NOT_SYNCED;
        uint32_t lastSyncAttempt = 0;
        uint32_t lastSuccessfulSync = 0;
        time_t lastKnownTime = 0;

        void startSntp();
        void syncTime();
        void configureTimezone();
    };

} // namespace SQM
