#include "SqmeterSource.h"

#include "SqmeterReply.h"
#include <ArduinoJson.h>
#include <cctype>

namespace SQM
{
    namespace Safety
    {
        namespace
        {
            constexpr int HTTP_OK = 200;

            // A good reply, unless it is the first after a subscription and says safe.
            void recordMqttReply(SourceRecord &record, bool &seen, const SqmeterReply &reply, TimeMs nowMs)
            {
                const bool firstSinceSubscribe = !seen;
                seen = true;
                if (firstSinceSubscribe && !reply.unsafe())
                    return;
                record.recordGood(reply, nowMs);
            }

            void applyAvailability(SourceRecord &record, const std::string &payload, TimeMs nowMs)
            {
                const auto online = parseAvailability(payload);
                if (!online)
                {
                    record.recordFailure(Failure::Unreadable, "availability not online/offline", nowMs);
                    return;
                }
                record.availability = *online ? Availability::Online : Availability::Offline;
            }

            void applySafe(SourceRecord &record, const std::string &payload, TimeMs nowMs)
            {
                const auto safe = parseSafePayload(payload);
                if (!safe)
                {
                    record.recordFailure(Failure::Unreadable, "safe not 1/0", nowMs);
                    return;
                }
                // SQMeter publishes <base>/safety just before <base>/safe, so the
                // flags already held are the latest; a bare "1" never clears them.
                SqmeterReply reply = record.last;
                reply.safe = *safe;
                reply.evaluatedAgeMs = 0;
                recordMqttReply(record, record.safeSeen, reply, nowMs);
            }

            void applySafety(SourceRecord &record, const std::string &payload, TimeMs nowMs)
            {
                SqmeterReply reply;
                const ReplyError error = parseSafetyJson(payload, reply);
                if (error != ReplyError::None)
                {
                    record.recordFailure(Failure::Unreadable, std::string("safety ") + replyErrorText(error), nowMs);
                    return;
                }
                recordMqttReply(record, record.safetySeen, reply, nowMs);
            }
        } // namespace

        const char *loginRefusalText(int status, bool credentialsSent)
        {
            if (status != 401 && status != 403)
                return nullptr;
            if (credentialsSent)
                return "SQMeter refused the password";
            return "SQMeter wants a password";
        }

        HttpCheck checkHttpReply(const HttpOutcome &outcome)
        {
            HttpCheck check;
            if (outcome.status <= 0)
            {
                check.failure = Failure::Unreachable;
                check.detail = outcome.error;
                return check;
            }
            check.failure = Failure::Unreadable;
            const char *login = loginRefusalText(outcome.status, outcome.credentials);
            if (login != nullptr)
                check.detail = login;
            else if (outcome.status != HTTP_OK)
                // Spec 020 FR-037: an address that answers, but isn't SQMeter's /api/safety.
                check.detail = "that address answered HTTP " + std::to_string(outcome.status) + ", not like SQMeter's /api/safety";
            else if (const ReplyError error = parseSafetyJson(outcome.body, check.reply); error != ReplyError::None)
                check.detail = std::string("that address answered, but not like SQMeter's /api/safety (") + replyErrorText(error) + ")";
            else
                check.failure = Failure::None;
            return check;
        }

        void recordHttpReply(SourceRecord &record, const HttpOutcome &outcome, TimeMs nowMs)
        {
            const HttpCheck check = checkHttpReply(outcome);
            if (check.failure != Failure::None)
                record.recordFailure(check.failure, check.detail, nowMs);
            else
                record.recordGood(check.reply, nowMs);
        }

        void writeTestResult(JsonObject out, const HttpOutcome &outcome)
        {
            const HttpCheck check = checkHttpReply(outcome);
            if (check.failure == Failure::Unreachable)
                out["error"] = "SQMeter not answering: " + (check.detail.empty() ? std::string("no reply") : check.detail);
            else if (check.failure == Failure::Unreadable)
            {
                std::string error = check.detail;
                error[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(error[0])));
                out["error"] = error;
            }
            else
            {
                out["safe"] = !check.reply.unsafe();
                out["reasonFlags"] = check.reply.reasonFlags;
                out["text"] =
                    check.reply.unsafe() ? "SQMeter says unsafe: " + reasonFlagsText(check.reply.reasonFlags) : "SQMeter says safe";
            }
        }

        MqttTopics MqttTopics::under(const std::string &base)
        {
            return MqttTopics{base + "/availability", base + "/safe", base + "/safety"};
        }

        void applyMqttMessage(
            SourceRecord &record, const MqttTopics &topics, const std::string &topic, const std::string &payload, TimeMs nowMs)
        {
            if (topic == topics.availability)
                applyAvailability(record, payload, nowMs);
            else if (topic == topics.safe)
                applySafe(record, payload, nowMs);
            else if (topic == topics.safety)
                applySafety(record, payload, nowMs);
        }
    } // namespace Safety
} // namespace SQM
