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
            // At rest, a change of the state key is published at most this often.
            constexpr uint32_t CHANGE_MIN_MS = 1000;
            // While the roof moves or a close waits, only a change of its state,
            // fault or waiting close, at most this often and at most
            // BUSY_MAX_PUBLISHES times until it is at rest again (research R4:
            // a write to a half-open link can hold the main loop for seconds,
            // and the roof's checks need that loop every 5 s).
            constexpr uint32_t BUSY_MIN_MS = 5000;
            constexpr uint8_t BUSY_MAX_PUBLISHES = 2;
            // A payload longer than this (after trimming) is never a command.
            constexpr size_t MAX_COMMAND_PAYLOAD = 16;
            // The MQTT socket's time on the main loop (research R4): a write may
            // take REST_WRITE_BUDGET_MS at rest; while the roof is busy, all of
            // MQTT's writes and reads in one main-loop pass share
            // BUSY_PASS_BUDGET_MS, one write at most BUSY_WRITE_BUDGET_MS. A
            // write with no time left drops the connection instead of waiting.
            constexpr uint32_t REST_WRITE_BUDGET_MS = 1000;
            constexpr uint32_t BUSY_WRITE_BUDGET_MS = 250;
            constexpr uint32_t BUSY_PASS_BUDGET_MS = 500;

            // One main-loop pass's MQTT time.
            class PassBudget
            {
            public:
                void start(uint32_t nowMs) { startMs = nowMs; }
                // How long the next write may take; 0: none left (drop the connection).
                uint32_t writeBudgetMs(uint32_t nowMs, bool busy) const;
                // Whether MQTT may read (run PubSubClient's loop) now.
                bool mayRead(uint32_t nowMs, bool busy) const;

            private:
                uint32_t startMs = 0;
            };

            // `bytes` starts with a whole MQTT packet (fixed header, remaining
            // length and that many bytes), so PubSubClient can read it without
            // waiting for more. False while the length field is incomplete or
            // malformed.
            bool packetComplete(const uint8_t *bytes, size_t size);

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
                bool run = false;       // pass `command` and `origin` to RoofControl::command
                bool isCommand = false; // the payload names a command (even when not run)
                Roof::Command command = Roof::Command::Stop;
                // Always MQTT: not a person, so the remote-client rules apply
                // (no move from unknown, no fault clear, no stopping or reversing
                // a close in bad weather; RoofIo::checkCommand).
                Roof::Origin origin = Roof::Origin::Mqtt;
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

                bool operator==(const StateKey &other) const;
                bool operator!=(const StateKey &other) const { return !(*this == other); }
            };
            StateKey keyOf(const Snapshot &snapshot);

            // The automatic close is attempting (waiting for the park, closing,
            // or waiting to retry): MQTT writes are kept to a minimum then, as
            // while the roof moves (research R4). Not while held by a person,
            // unable to move or halted: those wait for a person.
            bool closeAttempting(const std::string &closePhase);
            // The roof moves, a close waits, or the automatic close is attempting.
            bool busy(const StateKey &key);

            // When the document is due: never published, or a new connection; at
            // rest, the key changed and CHANGE_MIN_MS has passed, or the interval
            // has passed. While busy (research R4: the roof moves, a close waits,
            // or the automatic close is attempting):
            // only a change of state, fault or waiting close, BUSY_MIN_MS after
            // the last publish, at most BUSY_MAX_PUBLISHES times, and nothing at
            // all once a publish has failed on this connection.
            class StateSchedule
            {
            public:
                bool due(const StateKey &key, uint32_t connection, uint32_t nowMs, uint32_t intervalMs) const;
                void published(const StateKey &key, uint32_t connection, uint32_t nowMs, bool ok);

            private:
                bool any = false;
                StateKey last;
                uint32_t lastConnection = 0;
                uint32_t lastMs = 0;
                bool failed = false;       // a publish on this connection failed
                uint8_t busyPublishes = 0; // since the roof was last at rest
            };
        } // namespace Mqtt
    } // namespace RoofIo
} // namespace SQM
