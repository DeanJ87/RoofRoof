// Spec 037 FR-004: live-update frames come from a few buffers kept for the
// device's lifetime; a buffer is reused once no client holds it, and a frame
// is skipped (never allocated afresh) while every buffer is held.

#include <unity.h>

#include "FramePool.h"

using SQM::HeapSafe::FramePool;

void setUp() {}
void tearDown() {}

void test_nothing_before_the_buffers_are_reserved()
{
    FramePool pool;
    TEST_ASSERT_NULL(pool.take(100).get());
    TEST_ASSERT_EQUAL(0, pool.freeSlots());
}

void test_a_frame_is_sized_without_reallocating()
{
    FramePool pool;
    pool.reserve();
    auto frame = pool.take(4400);
    TEST_ASSERT_NOT_NULL(frame.get());
    const uint8_t *data = frame->data();
    TEST_ASSERT_EQUAL(4400, frame->size());
    frame.reset();
    auto again = pool.take(FramePool::CAPACITY);
    TEST_ASSERT_EQUAL_PTR(data, again->data()); // the same block, reused
}

void test_a_held_buffer_is_not_handed_out_again()
{
    FramePool pool;
    pool.reserve();
    FramePool::Frame held[FramePool::SLOTS];
    for (auto &frame : held)
        frame = pool.take(10);
    TEST_ASSERT_EQUAL(0, pool.freeSlots());
    TEST_ASSERT_NULL(pool.take(10).get()); // a slow client holds them all: skipped
    auto queued = held[1];                 // a client's queue still holds it
    held[1].reset();
    TEST_ASSERT_NULL(pool.take(10).get());
    queued.reset();
    TEST_ASSERT_NOT_NULL(pool.take(10).get());
}

void test_too_long_or_empty_frames_are_refused()
{
    FramePool pool;
    pool.reserve();
    TEST_ASSERT_NULL(pool.take(0).get());
    TEST_ASSERT_NULL(pool.take(FramePool::CAPACITY + 1).get());
    TEST_ASSERT_EQUAL(FramePool::SLOTS, pool.freeSlots());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_before_the_buffers_are_reserved);
    RUN_TEST(test_a_frame_is_sized_without_reallocating);
    RUN_TEST(test_a_held_buffer_is_not_handed_out_again);
    RUN_TEST(test_too_long_or_empty_frames_are_refused);
    return UNITY_END();
}
