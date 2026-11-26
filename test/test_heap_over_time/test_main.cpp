// Spec 045: what keeps the heap from being cut up over a session. A live frame
// written in place in a pooled buffer (no pieces), the recent alerts in a
// fixed ring (no strings), texts cut to their room at a character boundary,
// the per-minute heap trend, and the census's map of what separates the free
// blocks.

#include <unity.h>

#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <vector>

#include "ChunkText.h"
#include "FixedRing.h"
#include "FramePool.h"
#include "HeapCensus.h"
#include "HeapTrend.h"
#include "JsonStream.h"

using namespace SQM::HeapSafe;

void setUp() {}
void tearDown() {}

namespace
{
    size_t allocations = 0;
    void *countingAllocate(size_t bytes)
    {
        ++allocations;
        return std::malloc(bytes);
    }
} // namespace

// ChunkText::over --------------------------------------------------------

void test_a_text_over_a_buffer_is_written_in_place_and_contiguous()
{
    uint8_t buffer[3000];
    std::memset(buffer, '#', sizeof(buffer));
    ChunkText text = ChunkText::over(buffer, sizeof(buffer));
    const std::string long_(2500, 'a');
    text.append(long_.c_str());
    text += "bc";
    TEST_ASSERT_TRUE(text.ok());
    TEST_ASSERT_EQUAL(2502, text.size());
    TEST_ASSERT_EQUAL_MEMORY(long_.data(), buffer, 2500); // across the 1 KB piece boundaries, in order
    TEST_ASSERT_EQUAL_MEMORY("bc", buffer + 2500, 2);
    TEST_ASSERT_EQUAL('#', buffer[2502]);
    TEST_ASSERT_EQUAL_STRING((long_ + "bc").c_str(), text.str().c_str());
}

void test_a_text_over_a_buffer_fails_past_its_end_and_frees_nothing()
{
    uint8_t buffer[1500];
    {
        ChunkText text = ChunkText::over(buffer, sizeof(buffer));
        const std::string fits(1500, 'x');
        TEST_ASSERT_EQUAL(1500, text.write(reinterpret_cast<const uint8_t *>(fits.data()), fits.size()));
        TEST_ASSERT_TRUE(text.ok());
        TEST_ASSERT_EQUAL(0, text.write('y'));
        TEST_ASSERT_FALSE(text.ok());
        TEST_ASSERT_EQUAL(1500, text.size());
        text.clear(); // the buffer stays the caller's
        TEST_ASSERT_TRUE(text.ok());
        text += "again";
        TEST_ASSERT_EQUAL_MEMORY("again", buffer, 5);
    } // destroyed: must not free the stack buffer (ASan/abort if it did)
    ChunkText none = ChunkText::over(nullptr, 100);
    TEST_ASSERT_EQUAL(0, none.write('z'));
    TEST_ASSERT_FALSE(none.ok());
}

void test_a_moved_text_over_a_buffer_still_frees_nothing()
{
    uint8_t buffer[2048];
    ChunkText first = ChunkText::over(buffer, sizeof(buffer));
    first += "moved";
    ChunkText second(std::move(first));
    TEST_ASSERT_EQUAL(5, second.size());
    ChunkText third;
    third += "own pieces";
    third = std::move(second); // frees third's own piece, not the buffer
    TEST_ASSERT_EQUAL_STRING("moved", third.str().c_str());
}

void test_a_status_frame_is_written_into_a_pooled_buffer_without_allocating()
{
    FramePool pool;
    pool.reserve();
    FramePool::Frame frame = pool.take(FramePool::CAPACITY);
    TEST_ASSERT_NOT_NULL(frame.get());
    const uint8_t *before = frame->data();
    ChunkText text = ChunkText::over(frame->data(), frame->size());
    allocations = 0;
    StaticJsonDocument<512> doc;
    JsonStream json(text);
    json.beginObject();
    doc["uptime"] = 12;
    doc["roof"] = "closed";
    json.members(doc);
    json.endObject();
    TEST_ASSERT_TRUE(text.ok());
    frame->resize(text.size());
    TEST_ASSERT_EQUAL_PTR(before, frame->data()); // shrunk in place
    TEST_ASSERT_EQUAL_STRING_LEN("{\"uptime\":12,\"roof\":\"closed\"}", frame->data(), frame->size());
    TEST_ASSERT_EQUAL(0, allocations);
    // Held by a client: the pool hands out the other one, then none.
    FramePool::Frame second = pool.take(10);
    TEST_ASSERT_NOT_NULL(second.get());
    TEST_ASSERT_NULL(pool.take(10).get());
    frame.reset();
    TEST_ASSERT_NOT_NULL(pool.take(FramePool::CAPACITY).get());
}

void test_pieces_from_the_heap_are_still_counted()
{
    allocations = 0;
    ChunkText text(MAX_PIECES, countingAllocate);
    const std::string body(2100, 'p');
    text.append(body.c_str());
    TEST_ASSERT_EQUAL(3, allocations); // the old path: a piece per KB
}

// copyCut and FixedRing -------------------------------------------------

void test_text_is_copied_whole_when_it_fits()
{
    char out[16];
    copyCut(out, sizeof(out), "Published");
    TEST_ASSERT_EQUAL_STRING("Published", out);
    copyCut(out, sizeof(out), nullptr);
    TEST_ASSERT_EQUAL_STRING("", out);
    copyCut(out, sizeof(out), "exactly15chars!"); // 15 + terminator
    TEST_ASSERT_EQUAL_STRING("exactly15chars!", out);
}

void test_text_is_cut_at_a_character_boundary_and_marked()
{
    char out[12];
    copyCut(out, sizeof(out), "The roof didn't close");
    TEST_ASSERT_EQUAL_STRING("The roof...", out);
    // "Sun 3.2° ≥": cutting mid-character backs off to the boundary.
    char small[10];
    copyCut(small, sizeof(small), "ab\xC2\xB0\xE2\x89\xA5xyz12"); // a b ° ≥ x y z 1 2
    TEST_ASSERT_EQUAL_STRING("ab\xC2\xB0...", small);
    char tiny[4];
    copyCut(tiny, sizeof(tiny), "abcdef"); // no room for the mark
    TEST_ASSERT_EQUAL_STRING("abc", tiny);
}

namespace
{
    struct Rec
    {
        uint32_t id = 0;
        char text[8] = {};
    };
} // namespace

void test_the_ring_keeps_the_newest_and_overwrites_the_oldest()
{
    FixedRing<Rec, 3> ring;
    TEST_ASSERT_TRUE(ring.empty());
    for (uint32_t id = 1; id <= 5; ++id)
    {
        Rec r;
        r.id = id;
        copyCut(r.text, sizeof(r.text), "x");
        ring.push(r);
    }
    TEST_ASSERT_EQUAL(3, ring.size());
    TEST_ASSERT_EQUAL(3, ring.at(0).id); // oldest kept
    TEST_ASSERT_EQUAL(5, ring.at(2).id); // newest
    Rec *found = ring.findNewest([](const Rec &r) { return r.id == 4; });
    TEST_ASSERT_NOT_NULL(found);
    copyCut(found->text, sizeof(found->text), "changed");
    TEST_ASSERT_EQUAL_STRING("changed", ring.at(1).text);
    TEST_ASSERT_NULL(ring.findNewest([](const Rec &r) { return r.id == 1; })); // overwritten
    ring.clear();
    TEST_ASSERT_EQUAL(0, ring.size());
}

// HeapTrend ---------------------------------------------------------------

void test_each_minute_keeps_its_lowest_readings()
{
    HeapTrend trend;
    TEST_ASSERT_EQUAL(0, trend.lowestLargest(15));
    trend.sample(5000, 61000, 70000);
    trend.sample(20000, 40000, 69000);
    trend.sample(59999, 50000, 65000);
    TEST_ASSERT_EQUAL(1, trend.size());
    TEST_ASSERT_EQUAL(40000, trend.at(0).lowestLargest);
    TEST_ASSERT_EQUAL(65000, trend.at(0).lowestFree);
    trend.sample(60000, 55000, 66000); // the next minute
    TEST_ASSERT_EQUAL(2, trend.size());
    TEST_ASSERT_EQUAL(55000, trend.at(0).lowestLargest);
    TEST_ASSERT_EQUAL(1, trend.at(0).index);
    TEST_ASSERT_EQUAL(40000, trend.lowestLargest(2));
    TEST_ASSERT_EQUAL(55000, trend.lowestLargest(1)); // the current minute only
    TEST_ASSERT_EQUAL(65000, trend.lowestFree(15));
}

void test_the_trend_holds_an_hour_and_forgets_older_minutes()
{
    HeapTrend trend;
    trend.sample(0, 1000, 1000); // a deep dip in minute 0
    for (uint32_t minute = 1; minute <= 70; ++minute)
        trend.sample(minute * HeapTrend::MINUTE_MS + 10, 30000 + minute, 60000);
    TEST_ASSERT_EQUAL(HeapTrend::MINUTES, trend.size());
    TEST_ASSERT_EQUAL(70, trend.at(0).index);
    TEST_ASSERT_EQUAL(11, trend.at(HeapTrend::MINUTES - 1).index);
    TEST_ASSERT_EQUAL(30000 + 56, trend.lowestLargest(15)); // minutes 56-70
    TEST_ASSERT_EQUAL(30000 + 11, trend.lowestLargest(60)); // the dip has left the hour
}

void test_minutes_without_readings_are_skipped_and_the_clock_may_wrap()
{
    HeapTrend trend;
    trend.sample(0xFFFFFFFFu - 30000, 20000, 50000); // 30 s before millis() wraps
    trend.sample(0xFFFFFFFFu - 1000, 25000, 50000);
    TEST_ASSERT_EQUAL(1, trend.size());
    trend.sample(5 * HeapTrend::MINUTE_MS, 33000, 52000); // after the wrap, 5+ minutes on
    TEST_ASSERT_EQUAL(2, trend.size());
    TEST_ASSERT_EQUAL(trend.at(1).index + 5, trend.at(0).index);
    TEST_ASSERT_EQUAL(20000, trend.lowestLargest(6));
    TEST_ASSERT_EQUAL(33000, trend.lowestLargest(5));      // minute index - 5 is outside 5
    trend.sample(5 * HeapTrend::MINUTE_MS - 120000, 1, 1); // from before the current minute: ignored
    TEST_ASSERT_EQUAL(33000, trend.at(0).lowestLargest);
}

// HeapCensus free map ------------------------------------------------------

void test_the_census_maps_what_separates_the_free_blocks()
{
    HeapCensus census;
    const uintptr_t r1 = 0x1000, r1End = 0x30000;
    census.add(r1, r1End, 0x1000, 4000, false); // free, nothing before it
    census.add(r1, r1End, 0x2000, 200, true);
    census.add(r1, r1End, 0x2100, 40, false); // a small hole: counted as in the way
    census.add(r1, r1End, 0x2200, 600, true);
    census.add(r1, r1End, 0x3000, 20000, false);
    census.add(r1, r1End, 0x8000, 100, true);
    const uintptr_t r2 = 0x40000, r2End = 0x60000;
    census.add(r2, r2End, 0x40000, 300, true); // a new region starts afresh
    census.add(r2, r2End, 0x41000, 8000, false);
    TEST_ASSERT_EQUAL(3, census.freeSpanCount());
    TEST_ASSERT_EQUAL(0, census.freeSpan(0).usedBefore);
    TEST_ASSERT_EQUAL(20000, census.freeSpan(1).bytes);
    TEST_ASSERT_EQUAL(800, census.freeSpan(1).usedBefore);
    TEST_ASSERT_EQUAL(2, census.freeSpan(1).blocksBefore);
    TEST_ASSERT_EQUAL(600, census.freeSpan(1).largestBefore.bytes);
    TEST_ASSERT_EQUAL(0x2200, census.freeSpan(1).largestBefore.address);
    TEST_ASSERT_EQUAL(300, census.freeSpan(2).usedBefore); // not the 100 B of region 1
    ChunkText text;
    census.write(text);
    TEST_ASSERT_NOT_NULL(std::strstr(text.str().c_str(), "Free blocks of 1 KB or more"));
    TEST_ASSERT_NOT_NULL(std::strstr(text.str().c_str(), "20000 B  after    800 B in   2 blocks, largest   600 B"));
}

void test_the_free_map_is_bounded()
{
    HeapCensus census;
    for (size_t i = 0; i < HeapCensus::FREE_MAP + 10; ++i)
    {
        census.add(0x1000, 0x100000, 0x1000 + i * 0x2000, 16, true);
        census.add(0x1000, 0x100000, 0x1010 + i * 0x2000, 2048, false);
    }
    TEST_ASSERT_EQUAL(HeapCensus::FREE_MAP, census.freeSpanCount());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_text_over_a_buffer_is_written_in_place_and_contiguous);
    RUN_TEST(test_a_text_over_a_buffer_fails_past_its_end_and_frees_nothing);
    RUN_TEST(test_a_moved_text_over_a_buffer_still_frees_nothing);
    RUN_TEST(test_a_status_frame_is_written_into_a_pooled_buffer_without_allocating);
    RUN_TEST(test_pieces_from_the_heap_are_still_counted);
    RUN_TEST(test_text_is_copied_whole_when_it_fits);
    RUN_TEST(test_text_is_cut_at_a_character_boundary_and_marked);
    RUN_TEST(test_the_ring_keeps_the_newest_and_overwrites_the_oldest);
    RUN_TEST(test_each_minute_keeps_its_lowest_readings);
    RUN_TEST(test_the_trend_holds_an_hour_and_forgets_older_minutes);
    RUN_TEST(test_minutes_without_readings_are_skipped_and_the_clock_may_wrap);
    RUN_TEST(test_the_census_maps_what_separates_the_free_blocks);
    RUN_TEST(test_the_free_map_is_bounded);
    return UNITY_END();
}
