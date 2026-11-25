// Spec 038 FR-001: the heap census behind GET /api/diag/heap. Blocks are
// counted per region and per size, the largest blocks in use are kept largest
// first, and a task's stack is named by the block it starts in.

#include <unity.h>

#include <string>

#include "HeapCensus.h"

using SQM::HeapSafe::ChunkText;
using SQM::HeapSafe::HeapCensus;
using SQM::HeapSafe::TaskStack;

void setUp() {}
void tearDown() {}

void test_sizes_fall_in_their_buckets()
{
    TEST_ASSERT_EQUAL(0, HeapCensus::bucketOf(1));
    TEST_ASSERT_EQUAL(0, HeapCensus::bucketOf(32));
    TEST_ASSERT_EQUAL(1, HeapCensus::bucketOf(33));
    TEST_ASSERT_EQUAL(3, HeapCensus::bucketOf(1024));
    TEST_ASSERT_EQUAL(6, HeapCensus::bucketOf(8192));
    TEST_ASSERT_EQUAL(7, HeapCensus::bucketOf(8193));
}

void test_regions_add_up_their_free_space_and_largest_block()
{
    HeapCensus census;
    census.add(0x1000, 0x9000, 0x1000, 100, true);
    census.add(0x1000, 0x9000, 0x1100, 3000, false);
    census.add(0x1000, 0x9000, 0x2000, 5000, false);
    census.add(0x20000, 0x30000, 0x20000, 64, false);
    TEST_ASSERT_EQUAL(2, census.regionCount());
    TEST_ASSERT_EQUAL(0x8000, census.region(0).bytes);
    TEST_ASSERT_EQUAL(8000, census.region(0).freeBytes);
    TEST_ASSERT_EQUAL(5000, census.region(0).largestFree);
    TEST_ASSERT_EQUAL(1, census.region(0).usedBlocks);
    TEST_ASSERT_EQUAL(2, census.region(0).freeBlocks);
    TEST_ASSERT_EQUAL(64, census.region(1).largestFree);
    TEST_ASSERT_EQUAL(1, census.usedBucket(HeapCensus::bucketOf(100)).count);
    TEST_ASSERT_EQUAL(5000, census.freeBucket(HeapCensus::bucketOf(5000)).bytes);
}

void test_regions_past_the_limit_stop_the_walk()
{
    HeapCensus census;
    for (uintptr_t i = 0; i < HeapCensus::MAX_REGIONS; ++i)
        TEST_ASSERT_TRUE(census.add(i * 0x1000, i * 0x1000 + 0x800, i * 0x1000, 16, true));
    TEST_ASSERT_FALSE(census.add(0x100000, 0x100800, 0x100000, 16, true));
}

void test_the_largest_blocks_in_use_are_kept_largest_first()
{
    HeapCensus census;
    for (size_t i = 1; i <= HeapCensus::TOP_USED + 4; ++i)
        census.add(0x1000, 0x90000, 0x1000 + i * 0x100, i * 10, true);
    census.add(0x1000, 0x90000, 0x80000, 99999, false); // free: never listed
    TEST_ASSERT_EQUAL(HeapCensus::TOP_USED, census.topCount());
    TEST_ASSERT_EQUAL((HeapCensus::TOP_USED + 4) * 10, census.top(0).bytes);
    for (size_t i = 1; i < census.topCount(); ++i)
        TEST_ASSERT_TRUE(census.top(i - 1).bytes >= census.top(i).bytes);
    TEST_ASSERT_EQUAL(50, census.top(HeapCensus::TOP_USED - 1).bytes);
}

void test_a_task_stack_is_named_by_the_block_it_starts_in()
{
    TaskStack tasks[2];
    tasks[0].name = "loopTask";
    tasks[0].base = 0x4010; // just past the block's header
    tasks[0].leastLeft = 3000;
    tasks[1].name = "IDLE0";
    tasks[1].base = 0x700000; // not on this heap
    HeapCensus census;
    census.watch(tasks, 2);
    census.add(0x1000, 0x90000, 0x4000, 10260, true);
    census.add(0x1000, 0x90000, 0x8000, 4000, true);
    TEST_ASSERT_EQUAL(10260, tasks[0].blockBytes);
    TEST_ASSERT_EQUAL(0, tasks[1].blockBytes);
    TEST_ASSERT_EQUAL_PTR(&tasks[0], census.stackAt(0x4000));
    TEST_ASSERT_NULL(census.stackAt(0x8000));

    ChunkText out;
    census.write(out);
    const std::string text = out.str();
    TEST_ASSERT_TRUE(text.find("10260 B  stack of loopTask") != std::string::npos);
    TEST_ASSERT_TRUE(text.find("loopTask          10260 B  least left   3000 B") != std::string::npos);
    TEST_ASSERT_TRUE(text.find("IDLE0                 - B") != std::string::npos);
    TEST_ASSERT_TRUE(text.find("Regions") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sizes_fall_in_their_buckets);
    RUN_TEST(test_regions_add_up_their_free_space_and_largest_block);
    RUN_TEST(test_regions_past_the_limit_stop_the_walk);
    RUN_TEST(test_the_largest_blocks_in_use_are_kept_largest_first);
    RUN_TEST(test_a_task_stack_is_named_by_the_block_it_starts_in);
    return UNITY_END();
}
