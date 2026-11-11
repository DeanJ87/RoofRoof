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
                StaticJsonDocument<192> filter;
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

            MountReading readMount(JsonObjectConst response, uint32_t nowMs)
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
                return reading;
            }
        } // namespace

        MountReading failedReading(ReadingStatus status, const std::string &detail, uint32_t nowMs)
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

        MountReading ApiV2::parseMountInfo(int httpStatus, const std::string &body, uint32_t nowMs) const
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

        ParkReply ApiV2::parsePark(int httpStatus, const std::string &body) const
        {
            DynamicJsonDocument doc(PARSE_DOC_BYTES);
            if (!readEnvelope(body, ResponseKind::Text, doc) || !doc["Success"].is<bool>())
                return ParkReply{ParkOutcome::Unreadable, httpText(httpStatus)};
            if (!doc["Success"].as<bool>())
                return ParkReply{ParkOutcome::Refused, errorText(doc.as<JsonVariantConst>(), httpStatus)};
            const std::string text = doc["Response"].is<const char *>() ? doc["Response"].as<const char *>() : "";
            if (httpStatus != HTTP_OK)
                return ParkReply{ParkOutcome::Unreadable, httpText(httpStatus)};
            if (text == "Parking")
                return ParkReply{ParkOutcome::Accepted, text};
            if (text == "Mount already parked")
                return ParkReply{ParkOutcome::AlreadyParked, text};
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
