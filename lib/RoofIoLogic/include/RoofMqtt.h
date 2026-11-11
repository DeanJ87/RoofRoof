#pragma once

// The roof over MQTT (spec 008, contracts/mqtt.md): the commands on
// <base>/roof/set and their answers on <base>/roof/result, the retained roof
// document on <base>/state and when it is due. A command that gets past
// parseCommand still goes through RoofControl::command with Roof::Origin::Mqtt,
// which applies RoofIo::checkCommand like every other source (constitution
// II, III, VIII).

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "RoofStatus.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace Mqtt
        {
            constexpr const char *STATE_SUBTOPIC = "state";
            constexpr const char *COMMAND_SUBTOPIC = "roof/set";
            constexpr const char *RESULT_SUBTOPIC = "roof/result";
            // A change of the state key is published at most this often.
            constexpr uint32_t CHANGE_MIN_MS = 250;
            // A payload longer than this (after trimming) is never a command.
            constexpr size_t MAX_COMMAND_PAYLOAD = 16;
            // The JSON pools for the documents, sized by test_state_document_fits
            // and test_result_document_fits (on the host, whose slots are bigger).
            constexpr size_t STATE_DOC_CAPACITY = 2048;
            constexpr size_t RESULT_DOC_CAPACITY = 1024;

            // How the broker delivered a message: live, or retained (left on the
            // broker and sent because of a new subscription, MQTT 3.1.1
            // §3.3.1.3). Unknown when the flag can't be read: treat as retained.
            enum class Delivery : uint8_t
            {
                Live,
                Retained,
                Unknown,
            };

            // The retained flag of a message PubSubClient 2.8.0 passed to its
            // callback. `topic` and `payload` MUST be the pointers PubSubClient
            // passed (into its receive buffer, research R2): the PUBLISH header
            // sits a fixed distance before the topic. Every field that can be
            // checked is (packet type, QoS, remaining length, topic length);
            // anything that doesn't match is Unknown.
            Delivery deliveryOf(const uint8_t *topic, size_t topicLength, const uint8_t *payload, size_t payloadLength);

            // What to do with a message on the command topic.
            struct Request
            {
                bool run = false;       // pass `command` to RoofControl::command(..., Origin::Mqtt)
                bool isCommand = false; // the payload names a command (even when not run)
                Roof::Command command = Roof::Command::Stop;
                std::string reason; // why not run: "switchedOff", "retained", "notACommand"
                std::string text;
            };

            // In this order: MQTT roof commands switched off; a delivery that
            // isn't known to be live; a payload that isn't OPEN, CLOSE or STOP
            // (any case, surrounding whitespace ignored).
            Request parseCommand(const std::string &payload, Delivery delivery, bool enabled);

            // The answer on <base>/roof/result.
            struct Result
            {
                const char *command = nullptr; // Roof::commandName, or null when not a command
                bool accepted = false;
                std::string reason;
                std::string text;
                Roof::State state = Roof::State::Unknown;
            };
            void writeResult(JsonObject out, const Result &result);

            // The document on <base>/state: the time (0 until the clock is
            // valid) and the status document's roof object (writeStatus).
            void writeState(JsonObject out, const Snapshot &snapshot, uint32_t epoch, bool clockValid);

            // What makes the document due at once when it changes.
            struct StateKey
            {
                Roof::State state = Roof::State::Unknown;
                Roof::FaultKind fault = Roof::FaultKind::None;
                bool positionKnown = false;
                bool closePending = false;
                std::string closePhase;
                Safety::Verdict weather = Safety::Verdict::Unknown;
                bool parkConfirmed = false;
                bool supplyLow = false;

                bool operator==(const StateKey &other) const;
                bool operator!=(const StateKey &other) const { return !(*this == other); }
            };
            StateKey keyOf(const Snapshot &snapshot);

            // When the document is due: never published or a new connection; the
            // key changed and CHANGE_MIN_MS has passed; at rest, the interval has
            // passed. While the roof moves or a close waits, only changes
            // (research R4: nothing periodic may hold the main loop then).
            class StateSchedule
            {
            public:
                bool due(const StateKey &key, uint32_t connection, uint32_t nowMs, uint32_t intervalMs) const;
                void published(const StateKey &key, uint32_t connection, uint32_t nowMs);

            private:
                bool any = false;
                StateKey last;
                uint32_t lastConnection = 0;
                uint32_t lastMs = 0;
            };
        } // namespace Mqtt
    } // namespace RoofIo
} // namespace SQM
