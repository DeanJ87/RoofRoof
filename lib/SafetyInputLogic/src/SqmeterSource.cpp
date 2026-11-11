#include "SqmeterSource.h"

#include "SqmeterReply.h"

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

        void recordHttpReply(SourceRecord &record, const HttpOutcome &outcome, TimeMs nowMs)
        {
            if (outcome.status <= 0)
            {
                record.recordFailure(Failure::Unreachable, outcome.error, nowMs);
                return;
            }
            if (outcome.status != HTTP_OK)
            {
                record.recordFailure(Failure::Unreadable, "HTTP " + std::to_string(outcome.status), nowMs);
                return;
            }
            SqmeterReply reply;
            const ReplyError error = parseSafetyJson(outcome.body, reply);
            if (error != ReplyError::None)
                record.recordFailure(Failure::Unreadable, replyErrorText(error), nowMs);
            else
                record.recordGood(reply, nowMs);
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
