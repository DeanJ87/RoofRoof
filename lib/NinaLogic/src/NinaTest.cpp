#include "NinaTest.h"

namespace SQM
{
    namespace Nina
    {
        std::string testUrl(const std::string &base, const Api &api)
        {
            const bool slash = !base.empty() && base.back() == '/';
            return (slash ? base.substr(0, base.size() - 1) : base) + api.mountInfoRequest().path;
        }

        MountReading testReading(const Api &api, int httpStatus, const std::string &body, const std::string &error, uint64_t nowMs)
        {
            if (httpStatus <= 0)
                return failedReading(ReadingStatus::Unreachable, error.empty() ? "no reply" : error, nowMs);
            return api.parseMountInfo(httpStatus, body, nowMs);
        }

        int writeTestAnswer(JsonObject out, const MountReading &reading)
        {
            const bool good = reading.status == ReadingStatus::Good;
            const bool answered = good || reading.status == ReadingStatus::ApiError || reading.status == ReadingStatus::Unreadable;
            out["reachable"] = answered;
            out["connected"] = good && reading.connected;
            out["atPark"] = good && reading.atPark;
            out["slewing"] = good && reading.slewing;
            if (good)
                return 200;
            const std::string why = reading.detail.empty() ? readingStatusName(reading.status) : reading.detail;
            out["error"] = answered ? "N.I.N.A. answered, but: " + why : "N.I.N.A. not responding: " + why;
            return 502;
        }
    } // namespace Nina
} // namespace SQM
