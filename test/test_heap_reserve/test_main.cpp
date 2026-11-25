// Spec 038 FR-003: the reserve the new-handler gives up when the heap runs
// out, so operator new retries instead of aborting; the loop takes it back.

#include <unity.h>

#include <cstdlib>
#include <new>

#include "HeapReserve.h"

using SQM::HeapSafe::HeapReserve;

namespace
{
    int allocations = 0;
    int releases = 0;
    bool failNext = false;
    void *fakeAllocate(size_t size)
    {
        if (failNext)
            return nullptr;
        ++allocations;
        return std::malloc(size);
    }
    void fakeRelease(void *block)
    {
        ++releases;
        std::free(block);
    }
} // namespace

void setUp()
{
    allocations = 0;
    releases = 0;
    failNext = false;
}
void tearDown() {}

void test_the_reserve_is_taken_once()
{
    HeapReserve reserve(fakeAllocate, fakeRelease);
    TEST_ASSERT_FALSE(reserve.held());
    TEST_ASSERT_TRUE(reserve.refill());
    TEST_ASSERT_TRUE(reserve.refill());
    TEST_ASSERT_TRUE(reserve.held());
    TEST_ASSERT_EQUAL(1, allocations);
}

void test_spending_frees_it_once_and_counts()
{
    HeapReserve reserve(fakeAllocate, fakeRelease);
    reserve.refill();
    TEST_ASSERT_TRUE(reserve.spend());
    TEST_ASSERT_FALSE(reserve.spend()); // nothing left: the handler aborts
    TEST_ASSERT_FALSE(reserve.held());
    TEST_ASSERT_EQUAL(1, releases);
    TEST_ASSERT_EQUAL(1, reserve.spent());
}

void test_a_refill_that_cant_allocate_leaves_it_spent()
{
    HeapReserve reserve(fakeAllocate, fakeRelease);
    failNext = true;
    TEST_ASSERT_FALSE(reserve.refill());
    TEST_ASSERT_FALSE(reserve.held());
    failNext = false;
    TEST_ASSERT_TRUE(reserve.refill());
}

void test_the_loop_refills_only_with_room()
{
    TEST_ASSERT_FALSE(HeapReserve::refillFits(HeapReserve::REFILL_BLOCK_BYTES - 1));
    TEST_ASSERT_TRUE(HeapReserve::refillFits(HeapReserve::REFILL_BLOCK_BYTES));
}

void test_arming_installs_the_new_handler()
{
    SQM::HeapSafe::armReserve();
    TEST_ASSERT_TRUE(std::get_new_handler() == SQM::HeapSafe::onOutOfMemory);
    TEST_ASSERT_TRUE(SQM::HeapSafe::reserve().held());
    SQM::HeapSafe::onOutOfMemory(); // spends it; a second call would abort
    TEST_ASSERT_FALSE(SQM::HeapSafe::reserve().held());
    TEST_ASSERT_EQUAL(1, SQM::HeapSafe::reserve().spent());
    std::set_new_handler(nullptr);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_reserve_is_taken_once);
    RUN_TEST(test_spending_frees_it_once_and_counts);
    RUN_TEST(test_a_refill_that_cant_allocate_leaves_it_spent);
    RUN_TEST(test_the_loop_refills_only_with_room);
    RUN_TEST(test_arming_installs_the_new_handler);
    return UNITY_END();
}
