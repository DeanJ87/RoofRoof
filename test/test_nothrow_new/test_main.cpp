// Spec 037 FR-001: new (std::nothrow) never goes through the throwing
// operator new (which aborts in the firmware, built without exceptions) and
// returns nullptr when the allocation can't be made.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <new>

#include "NothrowNew.h"

namespace
{
    int throwingCalls = 0;

    struct Request
    {
        char headers[512];
    };
} // namespace

// The throwing form, replaced here only to see whether the nothrow forms call it.
void *operator new(std::size_t size)
{
    ++throwingCalls;
    void *p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void *operator new[](std::size_t size)
{
    return operator new(size);
}

void operator delete(void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    std::free(ptr);
}

void setUp()
{
    throwingCalls = 0;
}
void tearDown() {}

void test_nothrow_new_does_not_use_the_throwing_form()
{
    Request *request = new (std::nothrow) Request();
    TEST_ASSERT_NOT_NULL(request);
    delete request;
    char *bytes = new (std::nothrow) char[64];
    TEST_ASSERT_NOT_NULL(bytes);
    delete[] bytes;
    TEST_ASSERT_EQUAL(0, throwingCalls);
    TEST_ASSERT_TRUE(SQM::HeapSafe::nothrowNewReplaced);
}

void test_nothrow_new_returns_null_when_memory_runs_out()
{
    // More than any heap can give: null, never an abort.
    volatile std::size_t impossible = SIZE_MAX / 2;
    void *block = operator new(impossible, std::nothrow);
    TEST_ASSERT_NULL(block);
    char *bytes = new (std::nothrow) char[static_cast<std::size_t>(impossible)];
    TEST_ASSERT_NULL(bytes);
    TEST_ASSERT_EQUAL(0, throwingCalls);
}

void test_nothrow_new_of_zero_bytes_is_a_distinct_pointer()
{
    void *a = operator new(0, std::nothrow);
    void *b = operator new(0, std::nothrow);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);
    operator delete(a);
    operator delete(b);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nothrow_new_does_not_use_the_throwing_form);
    RUN_TEST(test_nothrow_new_returns_null_when_memory_runs_out);
    RUN_TEST(test_nothrow_new_of_zero_bytes_is_a_distinct_pointer);
    return UNITY_END();
}
