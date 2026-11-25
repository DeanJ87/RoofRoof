// Spec 029: the check value and the log ring kept across a restart (FR-005,
// FR-006; US4).

#include <unity.h>

#include <cstring>
#include <string>

#include "CrashTypes.h"
#include "LogRing.h"

using namespace SQM::Crash;

void setUp() {}
void tearDown() {}

namespace
{
    LogRing ring;

    std::string textAt(size_t index)
    {
        LogLine lines[RING_LINES];
        const size_t n = copy(ring, lines, RING_LINES);
        TEST_ASSERT_TRUE(index < n);
        return lines[index].text;
    }

    void fill(uint8_t value)
    {
        std::memset(&ring, value, sizeof(ring));
    }
} // namespace

void test_check_is_fnv1a()
{
    TEST_ASSERT_EQUAL_HEX32(0x811C9DC5u, check("", 0));
    TEST_ASSERT_EQUAL_HEX32(0xE40C292Cu, check("a", 1));
    TEST_ASSERT_EQUAL_HEX32(0xBF9CF968u, check("foobar", 6));
}

void test_copy_text_cuts_and_terminates()
{
    char out[5];
    copyText(out, sizeof(out), "abcdefgh");
    TEST_ASSERT_EQUAL_STRING("abcd", out);
    copyText(out, sizeof(out), nullptr);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_seal_and_intact()
{
    Snapshot snap{};
    snap.uptimeMs = 1234;
    TEST_ASSERT_FALSE(intact(snap));
    seal(snap);
    TEST_ASSERT_TRUE(intact(snap));
    snap.freeHeap = 1;
    TEST_ASSERT_FALSE(intact(snap));
}

void test_start_empties_random_memory()
{
    fill(0xA5);
    start(ring, true);
    TEST_ASSERT_EQUAL_UINT16(1, ring.boot);
    TEST_ASSERT_EQUAL_UINT16(0, ring.count);
    LogLine lines[RING_LINES];
    TEST_ASSERT_EQUAL_UINT32(0, copy(ring, lines, RING_LINES));
}

void test_start_keeps_and_counts_boots()
{
    fill(0);
    start(ring, false);
    push(ring, 10, Level::Info, "[Main] one");
    start(ring, true);
    TEST_ASSERT_EQUAL_UINT16(2, ring.boot);
    TEST_ASSERT_EQUAL_UINT16(1, ring.count);
    TEST_ASSERT_EQUAL_STRING("[Main] one", textAt(0).c_str());
    start(ring, false);
    TEST_ASSERT_EQUAL_UINT16(1, ring.boot);
    TEST_ASSERT_EQUAL_UINT16(0, ring.count);
}

void test_start_empties_out_of_range_header()
{
    fill(0);
    start(ring, false);
    push(ring, 10, Level::Info, "x");
    ring.head = RING_LINES; // sealed but impossible
    ring.check = check(&ring, offsetof(LogRing, check));
    start(ring, true);
    TEST_ASSERT_EQUAL_UINT16(0, ring.count);
    TEST_ASSERT_EQUAL_UINT16(1, ring.boot);
}

// Spec 031: a copy of the ring read into its own lines (src/CrashLog.cpp's log
// reply) once the ring has wrapped: each line once, oldest first. It repeated
// a few lines over and over before.
void test_copy_in_place_after_wrapping()
{
    for (int pushes : {60, 96, 50, 7})
    {
        fill(0);
        start(ring, false);
        for (int i = 0; i < pushes; ++i)
            push(ring, static_cast<uint32_t>(i), Level::Info, std::to_string(i).c_str());
        LogRing copyOf = ring;
        const size_t n = copy(copyOf, copyOf.lines, RING_LINES);
        const int kept = pushes < static_cast<int>(RING_LINES) ? pushes : static_cast<int>(RING_LINES);
        TEST_ASSERT_EQUAL_UINT32(kept, n);
        for (int i = 0; i < kept; ++i)
            TEST_ASSERT_EQUAL_STRING(std::to_string(pushes - kept + i).c_str(), copyOf.lines[i].text);
    }
}

void test_push_keeps_newest_oldest_first()
{
    fill(0);
    start(ring, false);
    for (int i = 0; i < 60; ++i)
        push(ring, static_cast<uint32_t>(i), Level::Info, std::to_string(i).c_str());
    LogLine lines[RING_LINES];
    TEST_ASSERT_EQUAL_UINT32(RING_LINES, copy(ring, lines, RING_LINES));
    TEST_ASSERT_EQUAL_STRING("10", lines[0].text);
    TEST_ASSERT_EQUAL_STRING("59", lines[RING_LINES - 1].text);
    TEST_ASSERT_EQUAL_UINT32(59, lines[RING_LINES - 1].ms);
    TEST_ASSERT_EQUAL_UINT32(3, copy(ring, lines, 3));
    TEST_ASSERT_EQUAL_STRING("10", lines[0].text);
}

void test_push_cuts_long_lines()
{
    fill(0);
    start(ring, false);
    const std::string longText(200, 'x');
    push(ring, 1, Level::Warn, longText.c_str());
    const std::string kept = textAt(0);
    TEST_ASSERT_EQUAL_UINT32(LINE_CHARS - 1, kept.size());
    LogLine lines[1];
    copy(ring, lines, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Level::Warn), lines[0].level);
}

void test_clean_text_drops_colours_and_controls()
{
    char out[LINE_CHARS];
    cleanText(out, sizeof(out), "\033[0;31mE (123) wifi: lost\033[0m\r\n");
    TEST_ASSERT_EQUAL_STRING("E (123) wifi: lost", out);
    cleanText(
        out,
        sizeof(out),
        "a\tb\x01"
        "c   ");
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_EQUAL_UINT32(0, cleanText(out, sizeof(out), nullptr));
}

void test_damaged_line_skipped()
{
    fill(0);
    start(ring, false);
    push(ring, 1, Level::Info, "one");
    push(ring, 2, Level::Info, "two");
    push(ring, 3, Level::Info, "three");
    ring.lines[1].text[0] = 'X';
    LogLine lines[RING_LINES];
    TEST_ASSERT_EQUAL_UINT32(2, copy(ring, lines, RING_LINES));
    TEST_ASSERT_EQUAL_STRING("one", lines[0].text);
    TEST_ASSERT_EQUAL_STRING("three", lines[1].text);
}

void test_level_names()
{
    TEST_ASSERT_EQUAL_STRING("warn", levelName(2));
    TEST_ASSERT_EQUAL_STRING("fatal", levelName(4));
    TEST_ASSERT_EQUAL_STRING("info", levelName(99));
}

void test_ring_fits_rtc_memory()
{
    // RTC slow memory has about 7.5 KB free (research R1); the ring takes most of the budget.
    TEST_ASSERT_TRUE(sizeof(LogRing) <= 4300);
    TEST_ASSERT_EQUAL_UINT32(84, sizeof(LogLine));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_is_fnv1a);
    RUN_TEST(test_copy_text_cuts_and_terminates);
    RUN_TEST(test_seal_and_intact);
    RUN_TEST(test_start_empties_random_memory);
    RUN_TEST(test_start_keeps_and_counts_boots);
    RUN_TEST(test_start_empties_out_of_range_header);
    RUN_TEST(test_push_keeps_newest_oldest_first);
    RUN_TEST(test_push_cuts_long_lines);
    RUN_TEST(test_clean_text_drops_colours_and_controls);
    RUN_TEST(test_damaged_line_skipped);
    RUN_TEST(test_level_names);
    RUN_TEST(test_ring_fits_rtc_memory);
    RUN_TEST(test_copy_in_place_after_wrapping);
    return UNITY_END();
}
