#include "SqmeterReply.h"

#include <ArduinoJson.h>

namespace SQM
{
    namespace Safety
    {
        namespace
        {
            // A whole number that fits uint32 (ArduinoJson: no negatives, no fractions).
            bool isCount(JsonVariantConst value)
            {
                return value.is<uint32_t>();
            }
        } // namespace

        ReplyError parseSafetyJson(const std::string &body, SqmeterReply &out)
        {
            StaticJsonDocument<128> filter;
            filter["safe"] = true;
            filter["reasonFlags"] = true;
            filter["evaluatedAgeMs"] = true;
            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, body, DeserializationOption::Filter(filter)))
                return ReplyError::NotJson;
            if (!doc.is<JsonObject>())
                return ReplyError::NotObject;
            if (!doc["safe"].is<bool>())
                return ReplyError::MissingSafe;
            if (!isCount(doc["reasonFlags"]))
                return ReplyError::MissingReasonFlags;
            JsonVariantConst age = doc["evaluatedAgeMs"];
            if (!age.isNull() && !isCount(age))
                return ReplyError::BadEvaluatedAge;

            out.safe = doc["safe"].as<bool>();
            out.reasonFlags = doc["reasonFlags"].as<uint32_t>();
            out.evaluatedAgeMs = age.isNull() ? 0 : age.as<uint32_t>();
            return ReplyError::None;
        }

        const char *replyErrorText(ReplyError error)
        {
            switch (error)
            {
            case ReplyError::None:
                return "ok";
            case ReplyError::NotJson:
                return "not JSON";
            case ReplyError::NotObject:
                return "not a JSON object";
            case ReplyError::MissingSafe:
                return "missing safe";
            case ReplyError::MissingReasonFlags:
                return "missing reasonFlags";
            case ReplyError::BadEvaluatedAge:
                return "bad evaluatedAgeMs";
            }
            return "unknown";
        }

        std::optional<bool> parseSafePayload(const std::string &payload)
        {
            if (payload == "1")
                return true;
            if (payload == "0")
                return false;
            return std::nullopt;
        }

        std::optional<bool> parseAvailability(const std::string &payload)
        {
            if (payload == "online")
                return true;
            if (payload == "offline")
                return false;
            return std::nullopt;
        }
    } // namespace Safety
} // namespace SQM
