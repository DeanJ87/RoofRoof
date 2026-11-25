// Spec 031: replies built without large or unchecked allocations: the text in
// pieces, the sectioned JSON writer, the buffered stream writer and the memory
// guard's rule (FR-001 to FR-005, FR-008, FR-009, FR-013).

#include <unity.h>

#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <utility>

#include "BufferedWriter.h"
#include "ChunkText.h"
#include "HeapGuard.h"
#include "JsonStream.h"

using namespace SQM::HeapSafe;

void setUp() {}
void tearDown() {}

namespace
{
    size_t allocations = 0;
    size_t allowAllocations = 0;

    void *countingAllocate(size_t bytes)
    {
        if (allocations >= allowAllocations)
            return nullptr;
        ++allocations;
        return std::malloc(bytes);
    }

    std::string readAll(const ChunkText &text, size_t window)
    {
        std::string out;
        uint8_t buffer[700];
        for (size_t offset = 0;;)
        {
            const size_t n = text.read(offset, buffer, window);
            if (n == 0)
                return out;
            out.append(reinterpret_cast<const char *>(buffer), n);
            offset += n;
        }
    }
} // namespace

// ---- ChunkText -------------------------------------------------------------

void test_text_spans_pieces_and_reads_back_by_offset()
{
    ChunkText text;
    std::string expected;
    for (int i = 0; i < 1000; ++i)
    {
        const std::string part = std::to_string(i) + ",";
        text += part;
        expected += part;
    }
    TEST_ASSERT_TRUE(text.ok());
    TEST_ASSERT_TRUE(text.size() > 2 * PIECE_BYTES);
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), text.str().c_str());
    // Windows that don't line up with the pieces, like the send buffer's.
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), readAll(text, 700).c_str());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), readAll(text, 1).c_str());
}

void test_text_takes_one_piece_at_a_time()
{
    allocations = 0;
    allowAllocations = 100;
    ChunkText text(MAX_PIECES, countingAllocate);
    text.append(std::string(PIECE_BYTES, 'a').c_str());
    TEST_ASSERT_EQUAL_UINT(1, allocations);
    text += 'b';
    TEST_ASSERT_EQUAL_UINT(2, allocations);
    TEST_ASSERT_EQUAL_UINT(PIECE_BYTES + 1, text.size());
}

void test_failed_allocation_marks_the_text_and_stops_writing()
{
    allocations = 0;
    allowAllocations = 2;
    ChunkText text(MAX_PIECES, countingAllocate);
    const std::string big(3 * PIECE_BYTES, 'x');
    text.append(big.c_str());
    TEST_ASSERT_FALSE(text.ok());
    TEST_ASSERT_EQUAL_UINT(2 * PIECE_BYTES, text.size());
    // Sticky: nothing more is written, even if memory comes back.
    allowAllocations = 100;
    TEST_ASSERT_EQUAL_UINT(0, text.write('y'));
    TEST_ASSERT_FALSE(text.ok());
    text.clear();
    TEST_ASSERT_TRUE(text.ok());
    TEST_ASSERT_EQUAL_UINT(0, text.size());
}

void test_cap_refuses_a_text_too_long()
{
    ChunkText text(2);
    text.append(std::string(2 * PIECE_BYTES, 'x').c_str());
    TEST_ASSERT_TRUE(text.ok());
    text += 'y';
    TEST_ASSERT_FALSE(text.ok());
    TEST_ASSERT_EQUAL_UINT(2 * PIECE_BYTES, text.size());
}

void test_text_moves_its_pieces()
{
    ChunkText a;
    a += "hello";
    ChunkText b(std::move(a));
    TEST_ASSERT_EQUAL_STRING("hello", b.str().c_str());
    TEST_ASSERT_EQUAL_UINT(0, a.size()); // NOLINT(bugprone-use-after-move): moved-from state is defined
    ChunkText c;
    c += "x";
    c = std::move(b);
    TEST_ASSERT_EQUAL_STRING("hello", c.str().c_str());
}

void test_serialize_json_into_pieces()
{
    DynamicJsonDocument doc(8192);
    JsonArray list = doc.createNestedArray("list");
    for (int i = 0; i < 300; ++i)
        list.add(i);
    std::string expected;
    serializeJson(doc, expected);
    ChunkText text;
    const size_t n = serializeJson(doc, text);
    TEST_ASSERT_EQUAL_UINT(expected.size(), n);
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), text.str().c_str());
}

// ---- JsonStream ------------------------------------------------------------

void test_sections_join_into_one_object()
{
    ChunkText text;
    JsonStream json(text);
    StaticJsonDocument<256> doc;
    json.beginObject();
    doc["a"] = 1;
    doc["b"] = "two";
    json.members(doc);
    doc.clear();
    doc.to<JsonObject>(); // an empty section writes nothing
    json.members(doc);
    doc.clear();
    doc["c"]["d"] = true;
    json.members(doc);
    json.endObject();
    TEST_ASSERT_TRUE(text.ok());
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":\"two\",\"c\":{\"d\":true}}", text.str().c_str());
    TEST_ASSERT_FALSE(json.overflowed());
}

void test_empty_first_section_adds_no_comma()
{
    ChunkText text;
    JsonStream json(text);
    StaticJsonDocument<128> doc;
    json.beginObject();
    doc.to<JsonObject>();
    json.members(doc);
    doc["x"] = 1;
    json.members(doc);
    json.endObject();
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", text.str().c_str());
}

void test_arrays_of_elements_and_nested_keys()
{
    ChunkText text;
    JsonStream json(text);
    StaticJsonDocument<128> doc;
    json.beginObject();
    doc["enabled"] = true;
    json.members(doc);
    json.beginArray("alerts");
    for (int i = 0; i < 3; ++i)
    {
        doc.clear();
        doc["id"] = i;
        json.element(doc);
    }
    json.endArray();
    json.beginArray("none");
    json.endArray();
    doc.clear();
    doc["k"] = 1;
    json.element(doc, "last");
    json.endObject();
    TEST_ASSERT_EQUAL_STRING(
        "{\"enabled\":true,\"alerts\":[{\"id\":0},{\"id\":1},{\"id\":2}],\"none\":[],\"last\":{\"k\":1}}", text.str().c_str());
    // Valid JSON with the same content.
    DynamicJsonDocument back(1024);
    TEST_ASSERT_FALSE(deserializeJson(back, text.str()));
    TEST_ASSERT_EQUAL(2, back["alerts"][2]["id"].as<int>());
}

void test_overflowed_section_is_written_and_flagged()
{
    ChunkText text;
    JsonStream json(text);
    StaticJsonDocument<JSON_OBJECT_SIZE(1)> doc;
    json.beginObject();
    doc["a"] = 1;
    doc["b"] = 2; // doesn't fit
    json.members(doc);
    json.endObject();
    TEST_ASSERT_TRUE(json.overflowed());
    TEST_ASSERT_TRUE(text.ok());
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", text.str().c_str());
}

void test_unallocated_pool_fails_the_reply()
{
    ChunkText text;
    JsonStream json(text);
    DynamicJsonDocument none(0); // what a failed pool allocation leaves
    json.beginObject();
    json.members(none);
    json.endObject();
    TEST_ASSERT_FALSE(text.ok());
}

// ---- BufferedWriter --------------------------------------------------------

namespace
{
    struct Sink
    {
        std::string text;
        size_t writes = 0;
        size_t limit = SIZE_MAX;
        size_t write(const uint8_t *data, size_t length)
        {
            ++writes;
            const size_t n = std::min(length, limit - text.size());
            text.append(reinterpret_cast<const char *>(data), n);
            return n;
        }
    };
} // namespace

void test_buffered_writer_batches_bytes()
{
    Sink sink;
    DynamicJsonDocument doc(4096);
    for (int i = 0; i < 100; ++i)
        doc[std::string("k") + std::to_string(i)] = i;
    std::string expected;
    serializeJson(doc, expected);
    BufferedWriter<Sink, 64> writer(sink);
    serializeJson(doc, writer);
    TEST_ASSERT_TRUE(writer.flush());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), sink.text.c_str());
    TEST_ASSERT_EQUAL_UINT((expected.size() + 63) / 64, sink.writes);
}

void test_buffered_writer_reports_a_short_write()
{
    Sink sink;
    sink.limit = 10;
    BufferedWriter<Sink, 8> writer(sink);
    const std::string text(40, 'z');
    writer.write(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    TEST_ASSERT_FALSE(writer.flush());
    TEST_ASSERT_TRUE(writer.failed());
    TEST_ASSERT_EQUAL_UINT(10, sink.text.size());
}

// ---- Guard -----------------------------------------------------------------

void test_alpaca_roof_commands_and_uploads_are_essential()
{
    TEST_ASSERT_TRUE(essential("/api/v1/dome/0/shutterstatus", false));
    TEST_ASSERT_TRUE(essential("/api/v1/dome/0/closeshutter", false));
    TEST_ASSERT_TRUE(essential("/management/v1/configureddevices", false));
    TEST_ASSERT_TRUE(essential("/setup", false));
    TEST_ASSERT_TRUE(essential("/setup/v1/dome/0/setup", false));
    TEST_ASSERT_TRUE(essential("/api/roof/stop", true));
    TEST_ASSERT_TRUE(essential("/api/roof/close", true));
    TEST_ASSERT_TRUE(essential("/api/update", true));
    TEST_ASSERT_TRUE(essential("/api/update/fs", true));
}

void test_pages_and_reads_are_not_essential()
{
    TEST_ASSERT_FALSE(essential("/api/status", false));
    TEST_ASSERT_FALSE(essential("/api/config", false));
    TEST_ASSERT_FALSE(essential("/api/config", true));
    TEST_ASSERT_FALSE(essential("/api/diag", false));
    TEST_ASSERT_FALSE(essential("/api/roof/inputs", false)); // a read, not a command
    TEST_ASSERT_FALSE(essential("/api/roof/calibration", false));
    TEST_ASSERT_FALSE(essential("/", false));
    TEST_ASSERT_FALSE(essential("/index.html", false));
    TEST_ASSERT_FALSE(essential("/ws/status", false));
    TEST_ASSERT_FALSE(essential("/setupx", false));
    TEST_ASSERT_FALSE(essential("/api/v10", false));
    TEST_ASSERT_FALSE(essential("/api/updates", true));
    TEST_ASSERT_FALSE(essential(nullptr, false));
}

void test_guard_limits_at_their_boundaries()
{
    TEST_ASSERT_TRUE(admit("/api/status", false, MIN_BLOCK_BYTES, MIN_FREE_BYTES));
    TEST_ASSERT_FALSE(admit("/api/status", false, MIN_BLOCK_BYTES - 1, MIN_FREE_BYTES));
    TEST_ASSERT_FALSE(admit("/api/status", false, MIN_BLOCK_BYTES, MIN_FREE_BYTES - 1));
    // The crash records' heap: 77 KB free, 8 KB largest: still admitted.
    TEST_ASSERT_TRUE(admit("/api/status", false, 8192, 77000));
    // Essential requests run whatever the heap.
    TEST_ASSERT_TRUE(admit("/api/v1/dome/0/shutterstatus", false, 0, 0));
    TEST_ASSERT_TRUE(admit("/api/roof/stop", true, 0, 0));
}

void test_frame_needs_its_length_plus_margin()
{
    TEST_ASSERT_TRUE(frameFits(4300, 4300 + FRAME_MARGIN_BYTES));
    TEST_ASSERT_FALSE(frameFits(4300, 4300 + FRAME_MARGIN_BYTES - 1));
    TEST_ASSERT_FALSE(frameFits(1, FRAME_MARGIN_BYTES - 1));
    TEST_ASSERT_TRUE(frameFits(0, FRAME_MARGIN_BYTES));
}

// Spec 037 FR-002: almost no memory left, a new connection is refused before
// the web server allocates its request.
void test_a_connection_is_accepted_only_with_room_for_its_request()
{
    TEST_ASSERT_FALSE(acceptFits(0));
    TEST_ASSERT_FALSE(acceptFits(MIN_ACCEPT_BLOCK_BYTES - 1));
    TEST_ASSERT_TRUE(acceptFits(MIN_ACCEPT_BLOCK_BYTES));
    TEST_ASSERT_TRUE(MIN_ACCEPT_BLOCK_BYTES < MIN_BLOCK_BYTES); // Alpaca still gets in when replies are refused
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_spans_pieces_and_reads_back_by_offset);
    RUN_TEST(test_text_takes_one_piece_at_a_time);
    RUN_TEST(test_failed_allocation_marks_the_text_and_stops_writing);
    RUN_TEST(test_cap_refuses_a_text_too_long);
    RUN_TEST(test_text_moves_its_pieces);
    RUN_TEST(test_serialize_json_into_pieces);
    RUN_TEST(test_sections_join_into_one_object);
    RUN_TEST(test_empty_first_section_adds_no_comma);
    RUN_TEST(test_arrays_of_elements_and_nested_keys);
    RUN_TEST(test_overflowed_section_is_written_and_flagged);
    RUN_TEST(test_unallocated_pool_fails_the_reply);
    RUN_TEST(test_buffered_writer_batches_bytes);
    RUN_TEST(test_buffered_writer_reports_a_short_write);
    RUN_TEST(test_alpaca_roof_commands_and_uploads_are_essential);
    RUN_TEST(test_pages_and_reads_are_not_essential);
    RUN_TEST(test_guard_limits_at_their_boundaries);
    RUN_TEST(test_frame_needs_its_length_plus_margin);
    RUN_TEST(test_a_connection_is_accepted_only_with_room_for_its_request);
    return UNITY_END();
}
