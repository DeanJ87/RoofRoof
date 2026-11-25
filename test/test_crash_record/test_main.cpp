// Spec 029: the crash record built at start, its blob form, the slots and the
// announced sequence (FR-007 to FR-010, FR-018; US1, US3).

#include <unity.h>

#include <cstring>
#include <string>

#include "CrashRecord.h"

using namespace SQM::Crash;

void setUp() {}
void tearDown() {}

namespace
{
    Capture panicCapture()
    {
        Capture c{};
        c.uptimeMs = 5231000;
        c.pc = 0x400d5f2c;
        c.frames = 3;
        c.backtrace[0] = 0x400d5f2c;
        c.backtrace[1] = 0x400d61a0;
        c.backtrace[2] = 0x40089e1d;
        c.core = 1;
        copyText(c.reason, sizeof(c.reason), "LoadProhibited");
        copyText(c.task, sizeof(c.task), "async_tcp");
        c.taskStackFree = 212;
        seal(c);
        return c;
    }

    Snapshot snapshot(uint32_t epoch)
    {
        Snapshot s{};
        s.uptimeMs = 5230500;
        s.epoch = epoch;
        s.freeHeap = 61224;
        s.minFreeHeap = 41020;
        s.largestBlock = 31732;
        s.stackLoop = 5980;
        s.stackRoof = 3416;
        s.stackNetwork = STACK_UNKNOWN;
        s.stackPoll = 2100;
        s.stackAlerts = 3900;
        copyText(s.roof, sizeof(s.roof), "closed");
        seal(s);
        return s;
    }

    RecordInputs inputs(ResetCode reset, const Capture *capture, const Snapshot *snap)
    {
        RecordInputs in;
        in.reset = reset;
        in.capture = capture;
        in.snapshot = snap;
        in.firmware = "0.1.0+dev";
        in.build = "3f2a9c0e11d4b7a2";
        in.seq = 7;
        return in;
    }
} // namespace

void test_kinds_and_tokens()
{
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::Panic), static_cast<uint8_t>(kindOf(ResetCode::Panic)));
    for (const ResetCode wdt : {ResetCode::InterruptWatchdog, ResetCode::TaskWatchdog, ResetCode::OtherWatchdog})
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::Watchdog), static_cast<uint8_t>(kindOf(wdt)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::Brownout), static_cast<uint8_t>(kindOf(ResetCode::Brownout)));
    for (const ResetCode none :
         {ResetCode::Unknown,
          ResetCode::PowerOn,
          ResetCode::External,
          ResetCode::Software,
          ResetCode::DeepSleep,
          ResetCode::Sdio,
          ResetCode::Usb,
          ResetCode::Jtag,
          ResetCode::Efuse,
          ResetCode::PowerGlitch,
          ResetCode::CpuLockup})
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::None), static_cast<uint8_t>(kindOf(none)));
    const char *tokens[] = {
        "unknown",
        "poweron",
        "external",
        "software",
        "panic",
        "watchdog",
        "watchdog",
        "watchdog",
        "deepsleep",
        "brownout",
        "sdio",
        "usb",
        "jtag",
        "efuse",
        "powerglitch",
        "cpulockup"};
    for (uint8_t code = 0; code < 16; ++code)
        TEST_ASSERT_EQUAL_STRING(tokens[code], resetToken(code));
    TEST_ASSERT_EQUAL_STRING("unknown", resetToken(99));
    TEST_ASSERT_EQUAL_STRING("brownout", kindName(Kind::Brownout));
}

void test_no_record_for_ordinary_restarts()
{
    Record out{};
    out.seq = 42;
    for (const ResetCode code : {ResetCode::PowerOn, ResetCode::Software, ResetCode::External, ResetCode::DeepSleep})
        TEST_ASSERT_FALSE(build(inputs(code, nullptr, nullptr), out));
    TEST_ASSERT_EQUAL_UINT32(42, out.seq);
}

void test_panic_record_from_capture_and_snapshot()
{
    const Capture c = panicCapture();
    const Snapshot s = snapshot(1795616000);
    LogLine lines[2]{};
    lines[0].ms = 5229120;
    lines[0].level = 1;
    copyText(lines[0].text, sizeof(lines[0].text), "[Config] Settings saved");
    lines[1].ms = 5229200;
    lines[1].level = 3;
    copyText(lines[1].text, sizeof(lines[1].text), "[Web] failed");
    RecordInputs in = inputs(ResetCode::Panic, &c, &s);
    in.log = lines;
    in.logCount = 2;
    Record r{};
    TEST_ASSERT_TRUE(build(in, r));
    TEST_ASSERT_EQUAL_UINT8(RECORD_VERSION, r.version);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::Panic), r.kind);
    TEST_ASSERT_EQUAL_UINT8(4, r.resetCode);
    TEST_ASSERT_EQUAL_UINT32(7, r.seq);
    TEST_ASSERT_EQUAL_UINT32(5231000, r.uptimeMs);
    TEST_ASSERT_EQUAL_HEX32(0x400d5f2c, r.capture.pc);
    TEST_ASSERT_EQUAL_UINT8(3, r.capture.frames);
    TEST_ASSERT_EQUAL_HEX32(0x40089e1d, r.capture.backtrace[2]);
    TEST_ASSERT_EQUAL_UINT8(1, r.capture.core);
    TEST_ASSERT_EQUAL_STRING("LoadProhibited", r.capture.reason);
    TEST_ASSERT_EQUAL_STRING("async_tcp", r.capture.task);
    TEST_ASSERT_EQUAL_STRING("0.1.0+dev", r.firmware);
    TEST_ASSERT_EQUAL_STRING("3f2a9c0e11d4b7a2", r.build);
    TEST_ASSERT_EQUAL_UINT32(61224, r.snapshot.freeHeap);
    TEST_ASSERT_EQUAL_UINT16(STACK_UNKNOWN, r.snapshot.stackNetwork);
    TEST_ASSERT_EQUAL_UINT16(2100, r.snapshot.stackPoll);
    TEST_ASSERT_EQUAL_UINT16(3900, r.snapshot.stackAlerts);
    TEST_ASSERT_EQUAL_UINT16(212, r.capture.taskStackFree);
    TEST_ASSERT_EQUAL_STRING("closed", r.snapshot.roof);
    TEST_ASSERT_EQUAL_UINT8(Record::CAPTURED | Record::SNAPSHOT | Record::TIMED, r.flags);
    // The crash came 500 ms after the snapshot: the same second.
    TEST_ASSERT_EQUAL_UINT32(1795616000, r.epoch);
    TEST_ASSERT_EQUAL_UINT8(2, r.logCount);
    TEST_ASSERT_EQUAL_STRING("[Web] failed", r.log[1].text);
    TEST_ASSERT_EQUAL_UINT8(3, r.log[1].level);
}

void test_epoch_advances_from_snapshot()
{
    Capture c = panicCapture();
    c.uptimeMs = 5230500 + 2500;
    seal(c);
    const Snapshot s = snapshot(1795616000);
    Record r{};
    TEST_ASSERT_TRUE(build(inputs(ResetCode::Panic, &c, &s), r));
    TEST_ASSERT_EQUAL_UINT32(1795616002, r.epoch);
}

void test_no_clock_no_time()
{
    const Capture c = panicCapture();
    const Snapshot s = snapshot(0);
    Record r{};
    TEST_ASSERT_TRUE(build(inputs(ResetCode::Panic, &c, &s), r));
    TEST_ASSERT_EQUAL_UINT32(0, r.epoch);
    TEST_ASSERT_EQUAL_UINT8(0, r.flags & Record::TIMED);
}

void test_task_watchdog_names_tasks()
{
    Capture c = panicCapture();
    copyText(c.reason, sizeof(c.reason), "Task watchdog got triggered");
    copyText(c.watchdogTasks, sizeof(c.watchdogTasks), "loopTask (CPU 1)");
    c.flags = Capture::CORRUPT | Capture::CONTINUES | Capture::FLASH_WRITE;
    seal(c);
    Record r{};
    TEST_ASSERT_TRUE(build(inputs(ResetCode::TaskWatchdog, &c, nullptr), r));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::Watchdog), r.kind);
    TEST_ASSERT_EQUAL_STRING("loopTask (CPU 1)", r.capture.watchdogTasks);
    TEST_ASSERT_EQUAL_UINT8(Record::CAPTURED, r.flags);
    TEST_ASSERT_EQUAL_UINT8(Capture::CORRUPT | Capture::CONTINUES | Capture::FLASH_WRITE, r.capture.flags);
    TEST_ASSERT_EQUAL_UINT32(5231000, r.uptimeMs);
    TEST_ASSERT_EQUAL_STRING("", r.snapshot.roof);
}

void test_brownout_from_snapshot_only()
{
    const Snapshot s = snapshot(1795616000);
    Record r{};
    TEST_ASSERT_TRUE(build(inputs(ResetCode::Brownout, nullptr, &s), r));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Kind::Brownout), r.kind);
    TEST_ASSERT_EQUAL_UINT8(Record::SNAPSHOT | Record::TIMED, r.flags);
    TEST_ASSERT_EQUAL_UINT32(5230500, r.uptimeMs);
    TEST_ASSERT_EQUAL_UINT8(0, r.capture.frames);
    TEST_ASSERT_EQUAL_HEX32(0, r.capture.pc);
    TEST_ASSERT_EQUAL_STRING("", r.capture.reason);
    TEST_ASSERT_EQUAL_STRING("", r.capture.task);
}

void test_panic_without_anything()
{
    Record r{};
    TEST_ASSERT_TRUE(build(inputs(ResetCode::Panic, nullptr, nullptr), r));
    TEST_ASSERT_EQUAL_UINT8(0, r.flags);
    TEST_ASSERT_EQUAL_UINT32(0, r.uptimeMs);
}

void test_log_tail_capped()
{
    LogLine lines[12]{};
    for (size_t i = 0; i < 12; ++i)
        copyText(lines[i].text, sizeof(lines[i].text), std::to_string(i).c_str());
    RecordInputs in = inputs(ResetCode::Panic, nullptr, nullptr);
    in.log = lines;
    in.logCount = 12;
    Record r{};
    TEST_ASSERT_TRUE(build(in, r));
    TEST_ASSERT_EQUAL_UINT8(RECORD_LINES, r.logCount);
    TEST_ASSERT_EQUAL_STRING("4", r.log[0].text);
    TEST_ASSERT_EQUAL_STRING("11", r.log[7].text);
}

void test_blob_round_trip_and_refusals()
{
    const Capture c = panicCapture();
    Record r{};
    TEST_ASSERT_TRUE(build(inputs(ResetCode::Panic, &c, nullptr), r));
    Record back{};
    TEST_ASSERT_TRUE(decode(&r, sizeof(r), back));
    TEST_ASSERT_EQUAL_MEMORY(&r, &back, sizeof(r));
    TEST_ASSERT_FALSE(decode(&r, sizeof(r) - 1, back));
    Record other = r;
    other.version = RECORD_VERSION + 1;
    TEST_ASSERT_FALSE(decode(&other, sizeof(other), back));
    other = r;
    other.kind = 0;
    TEST_ASSERT_FALSE(decode(&other, sizeof(other), back));
    other.kind = 9;
    TEST_ASSERT_FALSE(decode(&other, sizeof(other), back));
    TEST_ASSERT_FALSE(decode(nullptr, sizeof(r), back));
    TEST_ASSERT_TRUE(sizeof(Record) <= 1000); // about 30 NVS entries each (research R6)
}

void test_slots_keep_newest_three()
{
    TEST_ASSERT_EQUAL_UINT32(1, slot(7));
    TEST_ASSERT_EQUAL_UINT32(2, slot(8));
    TEST_ASSERT_EQUAL_UINT32(0, slot(9));
    TEST_ASSERT_EQUAL_UINT32(1, slot(10)); // replaces 7, the oldest
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_kinds_and_tokens);
    RUN_TEST(test_no_record_for_ordinary_restarts);
    RUN_TEST(test_panic_record_from_capture_and_snapshot);
    RUN_TEST(test_epoch_advances_from_snapshot);
    RUN_TEST(test_no_clock_no_time);
    RUN_TEST(test_task_watchdog_names_tasks);
    RUN_TEST(test_brownout_from_snapshot_only);
    RUN_TEST(test_panic_without_anything);
    RUN_TEST(test_log_tail_capped);
    RUN_TEST(test_blob_round_trip_and_refusals);
    RUN_TEST(test_slots_keep_newest_three);
    return UNITY_END();
}
