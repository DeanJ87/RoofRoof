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

            // Spec 030: SQMeter's "reasons" array, read on its own so a long one
            // never costs the verdict its reply: the words are only for people,
            // and when the pool runs out the part read so far is kept.
            std::string readReasons(const std::string &body)
            {
                StaticJsonDocument<32> filter;
                filter["reasons"] = true;
                StaticJsonDocument<512> doc;
                const DeserializationError error = deserializeJson(doc, body, DeserializationOption::Filter(filter));
                if (error && error != DeserializationError::NoMemory)
                    return std::string();
                std::string text;
                for (JsonVariantConst item : doc["reasons"].as<JsonArrayConst>())
                {
                    if (!item.is<const char *>())
                        continue;
                    text += text.empty() ? "" : "; ";
                    text += item.as<const char *>();
                    if (text.size() > 2 * MAX_REASONS_TEXT)
                        break; // enough to fill the kept text
                }
                return cleanReasonText(text);
            }
        } // namespace

        std::string cleanReasonText(const std::string &text, size_t maxBytes)
        {
            std::string out;
            out.reserve(text.size() < maxBytes ? text.size() : maxBytes + 1);
            for (const char c : text)
            {
                const unsigned char byte = static_cast<unsigned char>(c);
                if (byte < 0x20 || byte == 0x7F)
                    continue; // control characters never reach a log line or a notification
                out += c;
                if (out.size() > maxBytes)
                    break;
            }
            if (out.size() <= maxBytes)
                return out;
            size_t cut = maxBytes;
            while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
                --cut; // never split a UTF-8 character
            return out.substr(0, cut);
        }

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
            out.reasons = readReasons(body);
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
