#pragma once

// N.I.N.A. Advanced API (github.com/christian-photo/ninaAPI) behind an
// interface: what to request and how to read the reply, never the HTTP itself
// (src/SafetyPoller.cpp sends it). v2 is the only implementation; v3 (POST, 202,
// Basic auth) can be added as another Api. Endpoints and fields:
// specs/004-safety-inputs/research.md R2.

#include <cstdint>
#include <memory>
#include <string>

namespace SQM
{
    namespace Nina
    {
        // Which N.I.N.A. API the device talks to (the `park.nina.api` setting).
        enum class ApiVersion : uint8_t
        {
            V2,
        };

        enum class Method : uint8_t
        {
            Get,
            Post,
        };

        // One HTTP request, relative to the configured base URL. Credentials are
        // for v3's optional Basic auth; v2 leaves them empty.
        struct Request
        {
            Method method = Method::Get;
            std::string path;
            std::string username;
            std::string password;
        };

        // What the latest attempt to read the mount gave.
        enum class ReadingStatus : uint8_t
        {
            NotConfigured, // no N.I.N.A. URL set
            Waiting,       // configured, nothing tried yet
            Good,
            Unreachable, // no connection or no reply
            Unreadable,  // a reply that isn't the documented shape
            ApiError,    // N.I.N.A. answered with Success false
        };

        // The mount as N.I.N.A. reports it. connected/atPark/slewing mean
        // something only when status is Good.
        struct MountReading
        {
            ReadingStatus status = ReadingStatus::Waiting;
            bool connected = false;
            bool atPark = false;
            bool slewing = false;
            uint64_t takenAtMs = 0; // 64-bit milliseconds since start: never wraps
            std::string detail;     // why it isn't Good ("HTTP 500", "missing AtPark", N.I.N.A.'s Error)
        };

        // A failed attempt (no reply), stamped `nowMs`.
        MountReading failedReading(ReadingStatus status, const std::string &detail, uint64_t nowMs);

        enum class ParkOutcome : uint8_t
        {
            Accepted,      // N.I.N.A. started parking; only the park rule says it finished
            AlreadyParked, // v2 answers this as a success
            Refused,       // N.I.N.A. said no (e.g. mount not connected)
            Unreadable,    // no reply, or not the documented shape
        };

        struct ParkReply
        {
            ParkOutcome outcome = ParkOutcome::Unreadable;
            std::string detail;
        };

        // One version of the API.
        class Api
        {
        public:
            virtual ~Api() = default;
            virtual const char *name() const = 0;
            virtual Request mountInfoRequest() const = 0;
            virtual Request parkRequest() const = 0;
            // `httpStatus` is the HTTP status code (> 0) of a reply that arrived.
            virtual MountReading parseMountInfo(int httpStatus, const std::string &body, uint64_t nowMs) const = 0;
            virtual ParkReply parsePark(int httpStatus, const std::string &body) const = 0;
        };

        // The v2 API (plugin 2.2.15.0 or newer).
        class ApiV2 : public Api
        {
        public:
            const char *name() const override { return "v2"; }
            Request mountInfoRequest() const override;
            Request parkRequest() const override;
            MountReading parseMountInfo(int httpStatus, const std::string &body, uint64_t nowMs) const override;
            ParkReply parsePark(int httpStatus, const std::string &body) const override;
        };

        // The implementation for `version`.
        std::unique_ptr<Api> makeApi(ApiVersion version);
        // "v2" -> V2; false for anything else.
        bool parseApiVersion(const std::string &text, ApiVersion &out);
        const char *apiVersionName(ApiVersion version);
        // "Good", "Unreachable", ... for logs.
        const char *readingStatusName(ReadingStatus status);
    } // namespace Nina
} // namespace SQM
