#include "NinaApi.h"

#include <ArduinoJson.h>

// The v2 API: GET endpoints under /v2/api, every reply in the envelope
// {Response, Error, StatusCode, Success, Type}. Sources and the reasons for each
// rule: specs/004-safety-inputs/research.md R2 (D6, D7).

namespace SQM
{
    namespace Nina
    {
        namespace
        {
            constexpr const char *INFO_PATH = "/v2/api/equipment/mount/info";
            constexpr const char *PARK_PATH = "/v2/api/equipment/mount/park";
            // Spec 050: StopSlew() only (ninaAPI V2/Equipment/Mount.cs, MountStopSlew).
            constexpr const char *STOP_SLEW_PATH = "/v2/api/equipment/mount/slew/stop";
            constexpr int HTTP_OK = 200;
            // The filtered reply is small; the full info reply is several KB.
            constexpr size_t PARSE_DOC_BYTES = 512;

            std::string httpText(int httpStatus)
            {
                return "HTTP " + std::to_string(httpStatus);
            }

            // Which Response the reply carries: the mount's fields, or park's text.
            enum class ResponseKind : uint8_t
            {
                Mount,
                Text,
            };

            // Reads the envelope, keeping only what RoofRoof uses. False if the
            // body isn't a JSON object.
            bool readEnvelope(const std::string &body, ResponseKind kind, DynamicJsonDocument &doc)
            {
                StaticJsonDocument<256> filter; // spec 051: room for SiderealTime on a 64-bit host too
                filter["Success"] = true;
                filter["Error"] = true;
                if (kind == ResponseKind::Text)
                {
                    filter["Response"] = true;
                }
                else
                {
                    filter["Response"]["Connected"] = true;
                    filter["Response"]["AtPark"] = true;
                    filter["Response"]["Slewing"] = true;
                    filter["Response"]["SiderealTime"] = true; // spec 051: is the mount live?
                }
                const DeserializationError error = deserializeJson(doc, body, DeserializationOption::Filter(filter));
                return !error && doc.is<JsonObject>();
            }

            // Success false: N.I.N.A.'s own error text, or the HTTP status.
            std::string errorText(JsonVariantConst doc, int httpStatus)
            {
                const char *text = doc["Error"].is<const char *>() ? doc["Error"].as<const char *>() : "";
                return text[0] != '\0' ? std::string(text) : httpText(httpStatus);
            }

            // A boolean field of Response, or a reading naming the missing field.
            bool readFlag(JsonObjectConst response, const char *key, bool &out, MountReading &reading)
            {
                JsonVariantConst value = response[key];
                if (value.is<bool>())
                {
                    out = value.as<bool>();
                    return true;
                }
                reading.status = ReadingStatus::Unreadable;
                reading.detail = std::string(value.isNull() ? "missing " : "not true/false: ") + key;
                return false;
            }

            MountReading readMount(JsonObjectConst response, uint64_t nowMs)
            {
                MountReading reading = failedReading(ReadingStatus::Good, "", nowMs);
                if (!readFlag(response, "Connected", reading.connected, reading))
                    return failedReading(ReadingStatus::Unreadable, reading.detail, nowMs);
                if (!reading.connected)
                    return reading; // AtPark and Slewing mean nothing without a mount
                bool atPark = false;
                bool slewing = false;
                if (!readFlag(response, "AtPark", atPark, reading) || !readFlag(response, "Slewing", slewing, reading))
                    return failedReading(ReadingStatus::Unreadable, reading.detail, nowMs);
                reading.atPark = atPark;
                reading.slewing = slewing;
                // Spec 051: optional; missing or out of range, the freshness check is skipped.
                JsonVariantConst sidereal = response["SiderealTime"];
                const double hours = sidereal.is<double>() ? sidereal.as<double>() : -1.0;
                if (hours >= 0.0 && hours <= 24.0)
                {
                    reading.hasSiderealTime = true;
                    reading.siderealHours = hours;
                }
                return reading;
            }

            // A reply whose Response is text (park, stop slew): true with the
            // text, or false with why it failed in `failed`.
            bool readText(int httpStatus, const std::string &body, std::string &text, ParkReply &failed)
            {
                DynamicJsonDocument doc(PARSE_DOC_BYTES);
                if (!readEnvelope(body, ResponseKind::Text, doc) || !doc["Success"].is<bool>())
                {
                    failed = ParkReply{ParkOutcome::Unreadable, httpText(httpStatus)};
                    return false;
                }
                if (!doc["Success"].as<bool>())
                {
                    failed = ParkReply{ParkOutcome::Refused, errorText(doc.as<JsonVariantConst>(), httpStatus)};
                    return false;
                }
                if (httpStatus != HTTP_OK)
                {
                    failed = ParkReply{ParkOutcome::Unreadable, httpText(httpStatus)};
                    return false;
                }
                text = doc["Response"].is<const char *>() ? doc["Response"].as<const char *>() : "";
                return true;
            }
        } // namespace

        MountReading failedReading(ReadingStatus status, const std::string &detail, uint64_t nowMs)
        {
            MountReading reading;
            reading.status = status;
            reading.takenAtMs = nowMs;
            reading.detail = detail;
            return reading;
        }

        Request ApiV2::mountInfoRequest() const
        {
            return Request{Method::Get, INFO_PATH, "", ""};
        }

        Request ApiV2::parkRequest() const
        {
            return Request{Method::Get, PARK_PATH, "", ""};
        }

        MountReading ApiV2::parseMountInfo(int httpStatus, const std::string &body, uint64_t nowMs) const
        {
            DynamicJsonDocument doc(PARSE_DOC_BYTES);
            if (!readEnvelope(body, ResponseKind::Mount, doc) || !doc["Success"].is<bool>())
                return failedReading(
                    ReadingStatus::Unreadable, httpStatus == HTTP_OK ? "not a N.I.N.A. reply" : httpText(httpStatus), nowMs);
            if (!doc["Success"].as<bool>())
                return failedReading(ReadingStatus::ApiError, errorText(doc.as<JsonVariantConst>(), httpStatus), nowMs);
            // Success with another status: the reply contradicts itself.
            if (httpStatus != HTTP_OK)
                return failedReading(ReadingStatus::Unreadable, httpText(httpStatus), nowMs);
            if (!doc["Response"].is<JsonObject>())
                return failedReading(ReadingStatus::Unreadable, "Response is not an object", nowMs);
            return readMount(doc["Response"].as<JsonObjectConst>(), nowMs);
        }

        Request ApiV2::stopSlewRequest() const
        {
            return Request{Method::Get, STOP_SLEW_PATH, "", ""};
        }

        ParkReply ApiV2::parsePark(int httpStatus, const std::string &body) const
        {
            std::string text;
            ParkReply failed;
            if (!readText(httpStatus, body, text, failed))
                return failed;
            if (text == "Parking")
                return ParkReply{ParkOutcome::Accepted, text};
            if (text == "Mount already parked")
                return ParkReply{ParkOutcome::AlreadyParked, text};
            return ParkReply{ParkOutcome::Unreadable, "unexpected reply: " + text};
        }

        ParkReply ApiV2::parseStopSlew(int httpStatus, const std::string &body) const
        {
            std::string text;
            ParkReply failed;
            if (!readText(httpStatus, body, text, failed))
                return failed;
            if (text == "Stopped slew")
                return ParkReply{ParkOutcome::Accepted, text};
            return ParkReply{ParkOutcome::Unreadable, "unexpected reply: " + text};
        }

        std::unique_ptr<Api> makeApi(ApiVersion version)
        {
            switch (version)
            {
            case ApiVersion::V2:
                break;
            }
            return std::make_unique<ApiV2>();
        }

        bool parseApiVersion(const std::string &text, ApiVersion &out)
        {
            if (text != "v2")
                return false;
            out = ApiVersion::V2;
            return true;
        }

        const char *apiVersionName(ApiVersion version)
        {
            switch (version)
            {
            case ApiVersion::V2:
                break;
            }
            return "v2";
        }

        const char *readingStatusName(ReadingStatus status)
        {
            switch (status)
            {
            case ReadingStatus::NotConfigured:
                return "not configured";
            case ReadingStatus::Waiting:
                return "waiting";
            case ReadingStatus::Good:
                return "good";
            case ReadingStatus::Unreachable:
                return "unreachable";
            case ReadingStatus::Unreadable:
                return "unreadable";
            case ReadingStatus::ApiError:
                return "error";
            }
            return "unknown";
        }
    } // namespace Nina
} // namespace SQM
