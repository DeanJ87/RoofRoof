// The roof over MQTT (spec 008): the command gate (switched off, retained, not
// a command), the retained flag read from PubSubClient 2.8.0's buffer, the
// result and state documents, and when the state document is due.

#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "RoofMqtt.h"
#include "RoofStatus.h"

using namespace SQM;
using namespace SQM::RoofIo;
using Mqtt::Delivery;
using Roof::Command;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // A received PUBLISH exactly as PubSubClient 2.8.0's loop() hands it to the
    // callback: read into its buffer, the topic moved one byte forward over the
    // low byte of its length and NUL-terminated, the payload after it (and
    // after the packet id for QoS 1).
    struct Received
    {
        std::vector<uint8_t> buffer;
        const uint8_t *topic = nullptr;
        size_t topicLength = 0;
        const uint8_t *payload = nullptr;
        size_t payloadLength = 0;
    };

    void putLength(std::vector<uint8_t> &out, size_t length)
    {
        do
        {
            uint8_t digit = length % 128;
            length /= 128;
            if (length > 0)
                digit |= 0x80;
            out.push_back(digit);
        } while (length > 0);
    }

    Received receive(uint8_t header, const std::string &topic, const std::string &payload)
    {
        const bool qos1 = ((header >> 1) & 0x03) == 1;
        std::vector<uint8_t> packet{header};
        putLength(packet, 2 + topic.size() + (qos1 ? 2 : 0) + payload.size());
        const size_t llen = packet.size() - 1;
        packet.push_back(static_cast<uint8_t>(topic.size() >> 8));
        packet.push_back(static_cast<uint8_t>(topic.size() & 0xFF));
        packet.insert(packet.end(), topic.begin(), topic.end());
        if (qos1)
        {
            packet.push_back(0x12);
            packet.push_back(0x34);
        }
        packet.insert(packet.end(), payload.begin(), payload.end());

        Received r;
        r.buffer = packet;
        uint8_t *b = r.buffer.data();
        const size_t tl = (static_cast<size_t>(b[llen + 1]) << 8) + b[llen + 2];
        std::memmove(b + llen + 2, b + llen + 3, tl);
        b[llen + 2 + tl] = 0;
        r.topic = b + llen + 2;
        r.topicLength = std::strlen(reinterpret_cast<const char *>(r.topic));
        r.payload = qos1 ? b + llen + 3 + tl + 2 : b + llen + 3 + tl;
        r.payloadLength = r.buffer.size() - static_cast<size_t>(r.payload - b);
        return r;
    }

    Delivery deliveryOf(const Received &r)
    {
        return Mqtt::deliveryOf(r.topic, r.topicLength, r.payload, r.payloadLength);
    }

    const std::string TOPIC = "roofroof/roof/set";

    Snapshot closedRoof()
    {
        Snapshot s;
        s.status.state = Roof::State::Closed;
        s.status.positionKnown = true;
        s.status.positionMm = 0.0f;
        s.status.hall = Roof::HallLevel::Low;
        s.status.hallCounts = 700;
        s.supplyVolts = 12.44f;
        s.weather = Safety::Verdict::Safe;
        s.weatherText = "safe";
        s.parkConfirmed = true;
        s.parkText = "parked";
        return s;
    }
} // namespace

// --- The retained flag (research R2) ---

void test_live_and_retained_at_qos_0(void)
{
    TEST_ASSERT_TRUE(deliveryOf(receive(0x30, TOPIC, "OPEN")) == Delivery::Live);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x31, TOPIC, "OPEN")) == Delivery::Retained);
}

void test_live_and_retained_at_qos_1(void)
{
    TEST_ASSERT_TRUE(deliveryOf(receive(0x32, TOPIC, "STOP")) == Delivery::Live);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x33, TOPIC, "STOP")) == Delivery::Retained);
    // A redelivery (DUP) keeps its retained flag.
    TEST_ASSERT_TRUE(deliveryOf(receive(0x3A, TOPIC, "STOP")) == Delivery::Live);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x3B, TOPIC, "STOP")) == Delivery::Retained);
}

// Over 127 bytes the remaining length takes two bytes, and the header moves.
void test_a_two_byte_remaining_length(void)
{
    const std::string payload(200, 'x');
    TEST_ASSERT_TRUE(deliveryOf(receive(0x30, TOPIC, payload)) == Delivery::Live);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x31, TOPIC, payload)) == Delivery::Retained);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x33, TOPIC, payload)) == Delivery::Retained);
    // Exactly at the boundary: 127 needs one byte, 128 two.
    const size_t base = 2 + TOPIC.size();
    TEST_ASSERT_TRUE(deliveryOf(receive(0x31, TOPIC, std::string(127 - base, 'y'))) == Delivery::Retained);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x31, TOPIC, std::string(128 - base, 'y'))) == Delivery::Retained);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x30, TOPIC, std::string(128 - base, 'y'))) == Delivery::Live);
    TEST_ASSERT_TRUE(deliveryOf(receive(0x30, TOPIC, "")) == Delivery::Live);
}

// Anything that doesn't look like PubSubClient's layout is Unknown, never Live.
void test_a_layout_that_does_not_match_is_unknown(void)
{
    Received r = receive(0x30, TOPIC, "OPEN");
    const size_t headerAt = static_cast<size_t>(r.topic - r.buffer.data()) - 3; // one length byte

    Received wrongType = r;
    wrongType.buffer[headerAt] = 0x20;
    wrongType.topic = wrongType.buffer.data() + (r.topic - r.buffer.data());
    wrongType.payload = wrongType.buffer.data() + (r.payload - r.buffer.data());
    TEST_ASSERT_TRUE(deliveryOf(wrongType) == Delivery::Unknown);

    Received wrongQos = r;
    wrongQos.buffer[headerAt] = 0x32; // says QoS 1, laid out as QoS 0
    wrongQos.topic = wrongQos.buffer.data() + (r.topic - r.buffer.data());
    wrongQos.payload = wrongQos.buffer.data() + (r.payload - r.buffer.data());
    TEST_ASSERT_TRUE(deliveryOf(wrongQos) == Delivery::Unknown);

    Received wrongLength = r;
    wrongLength.buffer[headerAt + 1] ^= 0x01;
    wrongLength.topic = wrongLength.buffer.data() + (r.topic - r.buffer.data());
    wrongLength.payload = wrongLength.buffer.data() + (r.payload - r.buffer.data());
    TEST_ASSERT_TRUE(deliveryOf(wrongLength) == Delivery::Unknown);

    Received wrongTopicHigh = r;
    wrongTopicHigh.buffer[headerAt + 2] = 0x01;
    wrongTopicHigh.topic = wrongTopicHigh.buffer.data() + (r.topic - r.buffer.data());
    wrongTopicHigh.payload = wrongTopicHigh.buffer.data() + (r.payload - r.buffer.data());
    TEST_ASSERT_TRUE(deliveryOf(wrongTopicHigh) == Delivery::Unknown);

    // The payload neither one nor three bytes after the topic's end.
    TEST_ASSERT_TRUE(Mqtt::deliveryOf(r.topic, r.topicLength, r.payload + 1, r.payloadLength - 1) == Delivery::Unknown);
    TEST_ASSERT_TRUE(Mqtt::deliveryOf(r.topic, r.topicLength, r.topic, r.payloadLength) == Delivery::Unknown);
    TEST_ASSERT_TRUE(Mqtt::deliveryOf(r.payload, r.topicLength, r.topic, r.payloadLength) == Delivery::Unknown);
    TEST_ASSERT_TRUE(Mqtt::deliveryOf(nullptr, 0, r.payload, r.payloadLength) == Delivery::Unknown);
    TEST_ASSERT_TRUE(Mqtt::deliveryOf(r.topic, r.topicLength, nullptr, 0) == Delivery::Unknown);
}

// --- The command gate ---

void test_the_three_commands(void)
{
    const struct
    {
        const char *payload;
        Command command;
    } cases[] = {
        {"OPEN", Command::Open},
        {"CLOSE", Command::Close},
        {"STOP", Command::Stop},
        {"open", Command::Open},
        {" Close\n", Command::Close},
        {"\tsToP ", Command::Stop},
    };
    for (const auto &c : cases)
    {
        const Mqtt::Request r = Mqtt::parseCommand(c.payload, Delivery::Live, true);
        TEST_ASSERT_TRUE_MESSAGE(r.run, c.payload);
        TEST_ASSERT_TRUE(r.isCommand);
        TEST_ASSERT_TRUE(r.command == c.command);
        TEST_ASSERT_EQUAL_STRING("", r.reason.c_str());
    }
}

// No abort, no clear-fault, no position: only open, close and stop (spec 008 FR-007).
void test_only_three_commands(void)
{
    for (const char *payload :
         {"ABORT", "CLEAR", "CLEAR-FAULT", "clearFault", "50", "SET_POSITION", "", "   ", "OPEN_NOW", "OPENCLOSE", "{\"command\":\"open\"}", "ON"})
    {
        const Mqtt::Request r = Mqtt::parseCommand(payload, Delivery::Live, true);
        TEST_ASSERT_FALSE_MESSAGE(r.run, payload);
        TEST_ASSERT_FALSE(r.isCommand);
        TEST_ASSERT_EQUAL_STRING("notACommand", r.reason.c_str());
        TEST_ASSERT_EQUAL_STRING("Not a roof command: send OPEN, CLOSE or STOP", r.text.c_str());
    }
    // A long payload is refused without being looked at.
    const Mqtt::Request longOne = Mqtt::parseCommand(std::string(5000, ' ') + "OPEN" + std::string(5000, ' '), Delivery::Live, true);
    TEST_ASSERT_TRUE(longOne.run); // whitespace only around it: still OPEN
    const Mqtt::Request tooLong = Mqtt::parseCommand("OPEN" + std::string(100, 'x'), Delivery::Live, true);
    TEST_ASSERT_FALSE(tooLong.run);
    TEST_ASSERT_EQUAL_STRING("notACommand", tooLong.reason.c_str());
}

void test_switched_off_refuses_every_command(void)
{
    for (const char *payload : {"OPEN", "CLOSE", "STOP"})
        for (Delivery d : {Delivery::Live, Delivery::Retained, Delivery::Unknown})
        {
            const Mqtt::Request r = Mqtt::parseCommand(payload, d, false);
            TEST_ASSERT_FALSE(r.run);
            TEST_ASSERT_TRUE(r.isCommand);
            TEST_ASSERT_EQUAL_STRING("switchedOff", r.reason.c_str());
            TEST_ASSERT_EQUAL_STRING("Roof commands over MQTT are switched off (Settings > Network > MQTT)", r.text.c_str());
        }
    TEST_ASSERT_EQUAL_STRING("switchedOff", Mqtt::parseCommand("nonsense", Delivery::Live, false).reason.c_str());
}

// Constitution III: a retained OPEN would replay on every reconnect.
void test_retained_commands_never_run(void)
{
    for (const char *payload : {"OPEN", "CLOSE", "STOP"})
        for (Delivery d : {Delivery::Retained, Delivery::Unknown})
        {
            const Mqtt::Request r = Mqtt::parseCommand(payload, d, true);
            TEST_ASSERT_FALSE(r.run);
            TEST_ASSERT_TRUE(r.isCommand);
            TEST_ASSERT_EQUAL_STRING("retained", r.reason.c_str());
            TEST_ASSERT_EQUAL_STRING("A retained roof command is never acted on: publish it without the retain flag", r.text.c_str());
        }
}

// The whole path from the bytes PubSubClient hands over.
void test_retained_from_the_buffer_never_runs(void)
{
    const Received live = receive(0x30, TOPIC, "OPEN");
    const Received kept = receive(0x31, TOPIC, "OPEN");
    const std::string livePayload(reinterpret_cast<const char *>(live.payload), live.payloadLength);
    const std::string keptPayload(reinterpret_cast<const char *>(kept.payload), kept.payloadLength);
    TEST_ASSERT_TRUE(Mqtt::parseCommand(livePayload, deliveryOf(live), true).run);
    TEST_ASSERT_FALSE(Mqtt::parseCommand(keptPayload, deliveryOf(kept), true).run);
}

// --- The documents ---

void test_result_document(void)
{
    Mqtt::Result r;
    r.command = Roof::commandName(Command::Open);
    r.accepted = false;
    r.reason = "weatherNotSafe";
    r.text = "Weather not safe: unsafe: rain";
    r.state = Roof::State::Closed;
    DynamicJsonDocument doc(512);
    Mqtt::writeResult(doc.to<JsonObject>(), r);
    TEST_ASSERT_EQUAL_STRING("open", doc["command"]);
    TEST_ASSERT_FALSE(doc["accepted"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("weatherNotSafe", doc["reason"]);
    TEST_ASSERT_EQUAL_STRING("Weather not safe: unsafe: rain", doc["text"]);
    TEST_ASSERT_EQUAL_STRING("closed", doc["state"]);

    Mqtt::Result none;
    none.reason = "notACommand";
    DynamicJsonDocument other(512);
    Mqtt::writeResult(other.to<JsonObject>(), none);
    TEST_ASSERT_TRUE(other["command"].isNull());
    TEST_ASSERT_TRUE(other.containsKey("command"));
    TEST_ASSERT_EQUAL_STRING("unknown", other["state"]);
}

// FR-001: the roof object is the status document's, field for field.
void test_state_document_is_the_status_roof(void)
{
    const Snapshot s = closedRoof();
    DynamicJsonDocument state(4096);
    Mqtt::writeState(state.to<JsonObject>(), s, 1794480000u, true);
    TEST_ASSERT_EQUAL_UINT32(1794480000u, state["timestamp"].as<uint32_t>());
    TEST_ASSERT_TRUE(state["timeValid"].as<bool>());

    DynamicJsonDocument status(4096);
    writeStatus(status.createNestedObject("roof"), s);
    std::string a;
    std::string b;
    serializeJson(state["roof"], a);
    serializeJson(status["roof"], b);
    TEST_ASSERT_EQUAL_STRING(b.c_str(), a.c_str());
    TEST_ASSERT_EQUAL(3, state.as<JsonObject>().size());
}

void test_state_time_is_zero_until_the_clock_is_set(void)
{
    DynamicJsonDocument doc(4096);
    Mqtt::writeState(doc.to<JsonObject>(), closedRoof(), 12345u, false);
    TEST_ASSERT_EQUAL_UINT32(0, doc["timestamp"].as<uint32_t>());
    TEST_ASSERT_FALSE(doc["timeValid"].as<bool>());
}

// SC-005: the worst case stays under 1.5 KB (the MQTT buffer is 3 KB).
void test_state_document_fits(void)
{
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Error;
    s.status.fault = Roof::Fault{Roof::FaultKind::External, 4294967.0f, 4294967.0f};
    s.fault.fault = s.status.fault;
    s.fault.cause = FaultCause::ParkInterlock;
    s.status.moveElapsedMs = s.status.moveTimeoutMs = 4294967295u;
    s.currentSense = true;
    s.motorAmps = 123.45f;
    s.fittedSwitches = 2;
    s.rainFitted = true;
    s.closePending = true;
    s.watchdogTrips = s.maxStepGapMs = s.maxWatchdogBeatAgeMs = 4294967295u;
    s.closePhase = "heldByPerson";
    s.weatherText = std::string(MAX_CHECK_TEXT, 'w'); // applyChecks caps both
    s.parkText = std::string(MAX_CHECK_TEXT, 'p');
    DynamicJsonDocument doc(Mqtt::STATE_DOC_CAPACITY);
    Mqtt::writeState(doc.to<JsonObject>(), s, 4294967295u, true);
    TEST_ASSERT_FALSE(doc.overflowed());
    std::string text;
    serializeJson(doc, text);
    TEST_ASSERT_TRUE_MESSAGE(text.size() < 1536, text.c_str());
}

// The longest refusal (a stop refused with the longest weather text) fits.
void test_result_document_fits(void)
{
    Mqtt::Result r;
    r.command = "close";
    r.reason = "unsafeClose";
    r.text = std::string(400, 't');
    r.state = Roof::State::Closing;
    DynamicJsonDocument doc(Mqtt::RESULT_DOC_CAPACITY);
    Mqtt::writeResult(doc.to<JsonObject>(), r);
    TEST_ASSERT_FALSE(doc.overflowed());
}

// --- When the document is due (research R4) ---

void test_first_publish_and_each_connection(void)
{
    Mqtt::StateSchedule schedule;
    const Mqtt::StateKey key = Mqtt::keyOf(closedRoof());
    TEST_ASSERT_TRUE(schedule.due(key, 1, 0, 60000));
    schedule.published(key, 1, 0);
    TEST_ASSERT_FALSE(schedule.due(key, 1, 10, 60000));
    TEST_ASSERT_TRUE(schedule.due(key, 2, 10, 60000)); // reconnected: at once
}

void test_a_change_is_due_after_the_minimum_gap(void)
{
    Mqtt::StateSchedule schedule;
    Snapshot s = closedRoof();
    schedule.published(Mqtt::keyOf(s), 1, 1000);
    // Every field of the key makes a change.
    Snapshot changed[8] = {s, s, s, s, s, s, s, s};
    changed[0].status.state = Roof::State::Opening;
    changed[1].status.fault.kind = Roof::FaultKind::Timeout;
    changed[2].status.positionKnown = false;
    changed[3].closePending = true;
    changed[4].closePhase = "waitPark";
    changed[5].weather = Safety::Verdict::Unsafe;
    changed[6].parkConfirmed = false;
    changed[7].supplyVolts = 10.0f; // below the minimum
    for (const Snapshot &c : changed)
    {
        const Mqtt::StateKey key = Mqtt::keyOf(c);
        TEST_ASSERT_TRUE(key != Mqtt::keyOf(s));
        TEST_ASSERT_FALSE(schedule.due(key, 1, 1000 + Mqtt::CHANGE_MIN_MS - 1, 60000));
        TEST_ASSERT_TRUE(schedule.due(key, 1, 1000 + Mqtt::CHANGE_MIN_MS, 60000));
    }
    // Readings alone (Hall counts, supply above the minimum, position) are not a change.
    Snapshot readings = s;
    readings.status.hallCounts = 701;
    readings.supplyVolts = 12.0f;
    readings.status.positionMm = 3.0f;
    readings.weatherText = "safe (SQMeter MQTT)";
    TEST_ASSERT_TRUE(Mqtt::keyOf(readings) == Mqtt::keyOf(s));
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(readings), 1, 5000, 60000));
}

void test_the_interval_at_rest(void)
{
    Mqtt::StateSchedule schedule;
    const Mqtt::StateKey key = Mqtt::keyOf(closedRoof());
    schedule.published(key, 1, 0);
    TEST_ASSERT_FALSE(schedule.due(key, 1, 59999, 60000));
    TEST_ASSERT_TRUE(schedule.due(key, 1, 60000, 60000));
    // Across the millis() wrap.
    schedule.published(key, 1, 4294967000u);
    TEST_ASSERT_FALSE(schedule.due(key, 1, 100, 60000));
    TEST_ASSERT_TRUE(schedule.due(key, 1, 59704, 60000));
}

// Research R4: while the roof moves or a close waits, nothing periodic.
void test_nothing_periodic_while_moving(void)
{
    for (Roof::State moving : {Roof::State::Opening, Roof::State::Closing})
    {
        Snapshot s = closedRoof();
        s.status.state = moving;
        Mqtt::StateSchedule schedule;
        const Mqtt::StateKey key = Mqtt::keyOf(s);
        schedule.published(key, 1, 0);
        TEST_ASSERT_FALSE(schedule.due(key, 1, 600000, 1000));
        // A change still goes out (the end of the move, a fault).
        Snapshot stopped = s;
        stopped.status.state = Roof::State::Open;
        TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(stopped), 1, 600000, 1000));
        // So does a reconnect.
        TEST_ASSERT_TRUE(schedule.due(key, 2, 600000, 1000));
    }
    Snapshot waiting = closedRoof();
    waiting.status.state = Roof::State::Open;
    waiting.closePending = true;
    Mqtt::StateSchedule schedule;
    schedule.published(Mqtt::keyOf(waiting), 1, 0);
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(waiting), 1, 600000, 1000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_live_and_retained_at_qos_0);
    RUN_TEST(test_live_and_retained_at_qos_1);
    RUN_TEST(test_a_two_byte_remaining_length);
    RUN_TEST(test_a_layout_that_does_not_match_is_unknown);
    RUN_TEST(test_the_three_commands);
    RUN_TEST(test_only_three_commands);
    RUN_TEST(test_switched_off_refuses_every_command);
    RUN_TEST(test_retained_commands_never_run);
    RUN_TEST(test_retained_from_the_buffer_never_runs);
    RUN_TEST(test_result_document);
    RUN_TEST(test_state_document_is_the_status_roof);
    RUN_TEST(test_state_time_is_zero_until_the_clock_is_set);
    RUN_TEST(test_state_document_fits);
    RUN_TEST(test_result_document_fits);
    RUN_TEST(test_first_publish_and_each_connection);
    RUN_TEST(test_a_change_is_due_after_the_minimum_gap);
    RUN_TEST(test_the_interval_at_rest);
    RUN_TEST(test_nothing_periodic_while_moving);
    return UNITY_END();
}
