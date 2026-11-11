#include "RoofMqtt.h"

#include <cctype>

namespace SQM
{
    namespace RoofIo
    {
        namespace Mqtt
        {
            namespace
            {
                constexpr uint8_t PUBLISH = 0x30;
                constexpr size_t MAX_LENGTH_BYTES = 4;

                // How many bytes MQTT's remaining-length field takes (0: too long).
                size_t lengthBytes(size_t remaining)
                {
                    size_t bytes = 1;
                    for (size_t limit = 128; remaining >= limit; limit *= 128)
                        if (++bytes > MAX_LENGTH_BYTES)
                            return 0;
                    return bytes;
                }

                // The remaining-length field at `field` (`bytes` long) encodes `remaining`.
                bool lengthMatches(const uint8_t *field, size_t bytes, size_t remaining)
                {
                    size_t value = 0;
                    size_t scale = 1;
                    for (size_t i = 0; i < bytes; ++i)
                    {
                        const bool last = i + 1 == bytes;
                        if (((field[i] & 0x80) != 0) == last)
                            return false; // a continuation bit where there should be none, or none where there should be one
                        value += static_cast<size_t>(field[i] & 0x7F) * scale;
                        scale *= 128;
                    }
                    return value == remaining;
                }

                std::string trimmedLower(const std::string &payload)
                {
                    size_t begin = 0;
                    size_t end = payload.size();
                    while (begin < end && std::isspace(static_cast<unsigned char>(payload[begin])))
                        ++begin;
                    while (end > begin && std::isspace(static_cast<unsigned char>(payload[end - 1])))
                        --end;
                    if (end - begin > MAX_COMMAND_PAYLOAD)
                        return std::string();
                    std::string out = payload.substr(begin, end - begin);
                    for (char &c : out)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return out;
                }

                bool commandOf(const std::string &payload, Roof::Command &command)
                {
                    const std::string word = trimmedLower(payload);
                    if (word == "open")
                        command = Roof::Command::Open;
                    else if (word == "close")
                        command = Roof::Command::Close;
                    else if (word == "stop")
                        command = Roof::Command::Stop;
                    else
                        return false;
                    return true;
                }

                // What a publish while busy waits for.
                bool busyChange(const StateKey &a, const StateKey &b)
                {
                    return a.state != b.state || a.fault != b.fault || a.closePending != b.closePending ||
                           closeAttempting(a.closePhase) != closeAttempting(b.closePhase);
                }
            } // namespace

            bool closeAttempting(const std::string &closePhase)
            {
                return closePhase == "waitPark" || closePhase == "closing" || closePhase == "retryWait";
            }

            bool busy(const StateKey &key)
            {
                return key.state == Roof::State::Opening || key.state == Roof::State::Closing || key.closePending ||
                       closeAttempting(key.closePhase);
            }

            Delivery deliveryOf(const uint8_t *topic, size_t topicLength, const uint8_t *payload, size_t payloadLength)
            {
                if (topic == nullptr || payload == nullptr || payload <= topic)
                    return Delivery::Unknown;
                // PubSubClient's layout: QoS 0 payload right after the topic's NUL,
                // QoS 1 after a two-byte packet id as well.
                const size_t gap = static_cast<size_t>(payload - topic);
                unsigned qos = 0;
                if (gap == topicLength + 3)
                    qos = 1;
                else if (gap != topicLength + 1)
                    return Delivery::Unknown;
                const size_t remaining = 2 + topicLength + (qos == 1 ? 2 : 0) + payloadLength;
                const size_t bytes = lengthBytes(remaining);
                if (bytes == 0)
                    return Delivery::Unknown;
                // [header][remaining length][topic length high byte][topic moved over the low byte]
                const uint8_t *packet = topic - (bytes + 2);
                const uint8_t header = packet[0];
                if ((header & 0xF0) != PUBLISH || ((header >> 1) & 0x03) != qos)
                    return Delivery::Unknown;
                if (!lengthMatches(packet + 1, bytes, remaining) || packet[bytes + 1] != ((topicLength >> 8) & 0xFF))
                    return Delivery::Unknown;
                return (header & 0x01) != 0 ? Delivery::Retained : Delivery::Live;
            }

            Request parseCommand(const std::string &payload, Delivery delivery, bool enabled)
            {
                Request r;
                r.isCommand = commandOf(payload, r.command);
                if (!enabled)
                {
                    r.reason = "switchedOff";
                    r.text = "Roof commands over MQTT are switched off (Settings > Network > MQTT)";
                }
                else if (delivery != Delivery::Live)
                {
                    r.reason = "retained";
                    r.text = "A retained roof command replayed by the broker is never acted on: publish it without the retain flag";
                }
                else if (!r.isCommand)
                {
                    r.reason = "notACommand";
                    r.text = "Not a roof command: send OPEN, CLOSE or STOP";
                }
                else
                    r.run = true;
                return r;
            }

            void writeResult(JsonObject out, const Result &result)
            {
                if (result.command != nullptr)
                    out["command"] = result.command;
                else
                    out["command"] = nullptr;
                out["accepted"] = result.accepted;
                out["reason"] = result.reason;
                out["text"] = result.text;
                out["state"] = Roof::stateName(result.state);
            }

            void writeState(JsonObject out, const Snapshot &snapshot, uint32_t epoch, bool clockValid)
            {
                out["timestamp"] = clockValid ? epoch : 0U;
                out["timeValid"] = clockValid;
                writeStatus(out.createNestedObject("roof"), snapshot);
            }

            bool StateKey::operator==(const StateKey &o) const
            {
                return state == o.state && fault == o.fault && positionKnown == o.positionKnown && closePending == o.closePending &&
                       closePhase == o.closePhase && weather == o.weather && parkConfirmed == o.parkConfirmed;
            }

            StateKey keyOf(const Snapshot &snapshot)
            {
                StateKey key;
                key.state = snapshot.status.state;
                key.fault = snapshot.status.fault.kind;
                key.positionKnown = snapshot.status.positionKnown;
                key.closePending = snapshot.closePending;
                key.closePhase = snapshot.closePhase != nullptr ? snapshot.closePhase : "";
                key.weather = snapshot.weather;
                key.parkConfirmed = snapshot.parkConfirmed;
                return key;
            }

            bool StateSchedule::due(const StateKey &key, uint32_t connection, uint32_t nowMs, uint32_t intervalMs) const
            {
                if (!any || connection != lastConnection)
                    return true;
                const uint32_t since = nowMs - lastMs;
                if (busy(key))
                    return !failed && busyPublishes < BUSY_MAX_PUBLISHES && busyChange(key, last) && since >= BUSY_MIN_MS;
                if (key != last)
                    return since >= CHANGE_MIN_MS;
                return since >= intervalMs;
            }

            void StateSchedule::published(const StateKey &key, uint32_t connection, uint32_t nowMs, bool ok)
            {
                const bool sameConnection = any && connection == lastConnection;
                failed = (sameConnection && failed) || !ok;
                any = true;
                const uint8_t before = sameConnection ? busyPublishes : 0;
                busyPublishes = busy(key) ? static_cast<uint8_t>(before + 1) : 0;
                last = key;
                lastConnection = connection;
                lastMs = nowMs;
            }
            uint32_t PassBudget::writeBudgetMs(uint32_t nowMs, bool busy) const
            {
                if (!busy)
                    return REST_WRITE_BUDGET_MS;
                const uint32_t used = nowMs - startMs;
                if (used >= BUSY_PASS_BUDGET_MS)
                    return 0;
                const uint32_t left = BUSY_PASS_BUDGET_MS - used;
                return left < BUSY_WRITE_BUDGET_MS ? left : BUSY_WRITE_BUDGET_MS;
            }

            bool PassBudget::mayRead(uint32_t nowMs, bool busy) const
            {
                return !busy || nowMs - startMs < BUSY_PASS_BUDGET_MS;
            }

            bool packetComplete(const uint8_t *bytes, size_t size)
            {
                if (bytes == nullptr || size < 2)
                    return false;
                size_t remaining = 0;
                size_t scale = 1;
                for (size_t i = 1; i <= MAX_LENGTH_BYTES; ++i)
                {
                    if (i >= size)
                        return false; // the length field isn't all here yet
                    remaining += static_cast<size_t>(bytes[i] & 0x7F) * scale;
                    if ((bytes[i] & 0x80) == 0)
                        return size >= 1 + i + remaining;
                    scale *= 128;
                }
                return false; // a length longer than MQTT allows
            }
        } // namespace Mqtt
    } // namespace RoofIo
} // namespace SQM
