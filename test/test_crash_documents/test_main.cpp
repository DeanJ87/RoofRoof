// Spec 029: the crash documents (contracts/crashes.md): the records and the
// log as text (FR-012, FR-013; US1, US4).

#include <unity.h>

#include <string>
#include <vector>

#include "CrashDocuments.h"

using namespace SQM::Crash;

void setUp() {}
void tearDown() {}

namespace
{
    BootView boot()
    {
        BootView b;
        b.resetCode = 4;
        b.bootCount = 3;
        b.firmware = "0.1.0+dev";
        b.build = "3f2a9c0e11d4b7a2";
        return b;
    }

    Record fullRecord(uint32_t seq)
    {
        Capture c{};
        c.uptimeMs = 5231000;
        c.pc = 0x400d5f2c;
        c.frames = MAX_FRAMES;
        for (size_t i = 0; i < MAX_FRAMES; ++i)
            c.backtrace[i] = 0x400d0000u + static_cast<uint32_t>(i);
        c.core = 1;
        c.flags = Capture::CONTINUES;
        copyText(c.reason, sizeof(c.reason), std::string(REASON_CHARS, 'r').c_str());
        copyText(c.task, sizeof(c.task), "async_tcp");
        copyText(c.watchdogTasks, sizeof(c.watchdogTasks), std::string(WATCHDOG_TASKS_CHARS, 'w').c_str());
        c.taskStackFree = 96;
        seal(c);
        Snapshot s{};
        s.uptimeMs = 5230000;
        s.epoch = 1795616000;
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
        LogLine lines[RECORD_LINES]{};
        for (size_t i = 0; i < RECORD_LINES; ++i)
        {
            lines[i].ms = 1000 + static_cast<uint32_t>(i);
            lines[i].level = 3;
            copyText(lines[i].text, sizeof(lines[i].text), std::string(LINE_CHARS, '"').c_str());
        }
        RecordInputs in;
        in.reset = ResetCode::TaskWatchdog;
        in.capture = &c;
        in.snapshot = &s;
        static const std::string longVersion(FIRMWARE_CHARS, 'v');
        in.firmware = longVersion.c_str();
        in.build = "3f2a9c0e11d4b7a2";
        in.seq = seq;
        in.log = lines;
        in.logCount = RECORD_LINES;
        Record r{};
        build(in, r);
        return r;
    }

    Record brownout(uint32_t seq)
    {
        RecordInputs in;
        in.reset = ResetCode::Brownout;
        in.seq = seq;
        in.firmware = "0.1.0";
        Record r{};
        build(in, r);
        return r;
    }

    std::string recordText(const Record &r)
    {
        SQM::HeapSafe::ChunkText out;
        writeRecord(out, r);
        return out.str();
    }

    bool has(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }
} // namespace

void test_record_text()
{
    const std::string t = recordText(fullRecord(7));
    TEST_ASSERT_EQUAL_UINT32(0, t.find("Record 7: watchdog\n"));
    TEST_ASSERT_TRUE(has(t, "Time: 2026-11-25 14:13:21 UTC, 5231 s after start\n"));
    TEST_ASSERT_TRUE(has(t, "Reset reason: watchdog (6)\n"));
    TEST_ASSERT_TRUE(has(t, ", build 3f2a9c0e11d4b7a2\n"));
    TEST_ASSERT_TRUE(has(t, "Reason: " + std::string(REASON_CHARS - 1, 'r') + "\n"));
    TEST_ASSERT_TRUE(has(t, "Task: async_tcp, core 1, stack left 96 B\n"));
    TEST_ASSERT_TRUE(has(t, "Stuck tasks: " + std::string(WATCHDOG_TASKS_CHARS - 1, 'w') + "\n"));
    TEST_ASSERT_FALSE(has(t, "Flash write"));
    TEST_ASSERT_TRUE(has(t, "Backtrace: 0x400d5f2c 0x400d0000 0x400d0001"));
    TEST_ASSERT_TRUE(has(t, " 0x400d000f [continues]\n"));
    TEST_ASSERT_TRUE(has(t, "Memory: free 61224 B, lowest 41020 B, largest block 31732 B\n"));
    TEST_ASSERT_TRUE(has(t, "Stack left: loop 5980, roof 3416, async_tcp ?, safetyPoll 2100, alerts 3900 B\n"));
    TEST_ASSERT_TRUE(has(t, "Roof: closed\n"));
    TEST_ASSERT_TRUE(has(t, "Log before the restart:\n  1.000s ERROR "));
    TEST_ASSERT_TRUE(has(t, "  1.007s ERROR "));
}

void test_record_without_capture_or_snapshot()
{
    const std::string t = recordText(brownout(2));
    TEST_ASSERT_EQUAL_UINT32(0, t.find("Record 2: brownout\n"));
    TEST_ASSERT_TRUE(has(t, "Time: unknown (clock not set), 0 s after start\n"));
    TEST_ASSERT_TRUE(has(t, "Firmware: 0.1.0, build ?\n"));
    TEST_ASSERT_TRUE(has(t, "Reason: not captured\n"));
    TEST_ASSERT_FALSE(has(t, "Backtrace"));
    TEST_ASSERT_FALSE(has(t, "Memory"));
    TEST_ASSERT_FALSE(has(t, "Log before"));
}

void test_flash_write_and_corrupt_flags()
{
    Capture c{};
    c.pc = 0x40081234;
    c.flags = Capture::CORRUPT | Capture::FLASH_WRITE;
    copyText(c.reason, sizeof(c.reason), "Cache disabled but cached memory region accessed");
    copyText(c.task, sizeof(c.task), "roof_motion");
    c.taskStackFree = STACK_UNKNOWN;
    seal(c);
    RecordInputs in;
    in.reset = ResetCode::Panic;
    in.capture = &c;
    in.seq = 4;
    Record r{};
    build(in, r);
    const std::string t = recordText(r);
    TEST_ASSERT_TRUE(has(t, "Task: roof_motion, core 0, stack left ? B\n"));
    TEST_ASSERT_TRUE(has(t, "Flash write: in progress\n"));
    TEST_ASSERT_TRUE(has(t, "Backtrace: 0x40081234 [corrupt]\n"));
}

void test_crashes_document()
{
    const std::vector<Record> records = {fullRecord(8), brownout(7)};
    const std::string t = crashesText(boot(), records.data(), records.size());
    TEST_ASSERT_EQUAL_UINT32(
        0,
        t.find(
            "This start: panic (4), boot 3\nRunning: 0.1.0+dev, build 3f2a9c0e11d4b7a2\nRecords: 2 (the last 3 are kept)\n\nRecord 8: "));
    TEST_ASSERT_TRUE(has(t, "\n\nRecord 7: brownout\n"));
    BootView plain = boot();
    plain.resetCode = 1;
    plain.build = "";
    TEST_ASSERT_EQUAL_STRING(
        "This start: poweron (1), boot 3\nRunning: 0.1.0+dev, build ?\nRecords: 0 (the last 3 are kept)\n",
        crashesText(plain, nullptr, 0).c_str());
}

void test_log_text()
{
    std::vector<LogLine> lines(3);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        lines[i] = LogLine{};
        lines[i].boot = i == 0 ? 11 : 12;
        lines[i].level = static_cast<uint8_t>(i + 1);
        copyText(lines[i].text, sizeof(lines[i].text), ("line \"" + std::to_string(i) + "\"").c_str());
    }
    lines[0].ms = 5229120;
    lines[1].ms = 412;
    lines[2].ms = 7;
    TEST_ASSERT_EQUAL_STRING(
        "#11 5229.120s INFO  line \"0\"\n#12 0.412s WARN  line \"1\"\n#12 0.007s ERROR line \"2\"\n",
        logText(lines.data(), lines.size()).c_str());
    TEST_ASSERT_EQUAL_STRING("", logText(nullptr, 0).c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_record_text);
    RUN_TEST(test_record_without_capture_or_snapshot);
    RUN_TEST(test_flash_write_and_corrupt_flags);
    RUN_TEST(test_crashes_document);
    RUN_TEST(test_log_text);
    return UNITY_END();
}
