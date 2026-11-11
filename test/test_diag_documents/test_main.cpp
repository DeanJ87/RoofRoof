// Spec 017's documents (contracts/diagnostics.md): the live document, the
// summary list, one move and its CSV. Unreadable values are null (FR-004);
// the mode off is exactly {"enabled":false} (FR-005).

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"
#include "DiagDocuments.h"

using namespace SQM;
using namespace SQM::Diag;

void setUp() {}
void tearDown() {}

namespace
{
    RoofLive movingRoof()
    {
        RoofLive roof;
        roof.status.state = Roof::State::Opening;
        roof.status.target = Roof::End::Open;
        roof.status.driving = true;
        roof.status.positionKnown = true;
        roof.status.positionMm = 925.0f;
        roof.status.moveElapsedMs = 21400;
        roof.status.moveTimeoutMs = 63700;
        roof.status.hall = Roof::HallLevel::Between;
        roof.status.hallCounts = 2050;
        roof.output.command = Driver::Command::Forward;
        roof.output.duty = 255;
        roof.pins = Driver::outputsFor(Driver::Command::Forward, 255);
        roof.settings.currentSenseEnabled = true;
        roof.supplyVolts = 12.0f;
        roof.motorAmps = 3.5f;
        roof.budgetUsedMs[1] = 20900;
        roof.budgetUsedMs[0] = 100;
        roof.maxGapMs = 21;
        roof.log.phase = Phase::Cruise;
        roof.log.restVolts = 12.6f;
        roof.log.sagVolts = 0.6f;
        roof.log.moveId = 12;
        roof.log.moveActive = true;
        return roof;
    }

    DynamicJsonDocument live(const RoofLive &roof, const SystemLive &system)
    {
        DynamicJsonDocument doc(LIVE_DOC_CAPACITY);
        writeLive(doc.to<JsonObject>(), roof, system);
        TEST_ASSERT_FALSE_MESSAGE(doc.overflowed(), "the live document overflowed its pool");
        std::string text;
        serializeJson(doc, text);
        DynamicJsonDocument back(8192);
        TEST_ASSERT_FALSE(deserializeJson(back, text));
        return back;
    }

    template <typename Part> std::string whole(Part part)
    {
        std::string all;
        std::string out;
        for (size_t i = 0; part(i, out); ++i)
            all += out;
        return all;
    }

    Move sampleMove()
    {
        Move move;
        move.summary.id = 7;
        move.summary.startMs = 1000;
        move.summary.endMs = 2250;
        move.summary.result = Result::Arrived;
        move.summary.endState = Roof::State::Open;
        move.summary.target = Roof::End::Open;
        move.intervalMs = 250;
        move.count = 10;
        for (uint8_t i = 0; i < move.count; ++i)
        {
            move.points[i].ampsAvg = static_cast<uint16_t>(100 + i);
            move.points[i].ampsMax = static_cast<uint16_t>(200 + i);
            move.points[i].voltsMin = 1200;
            move.points[i].duty = 255;
        }
        move.points[3].ampsAvg = NONE;
        move.points[3].voltsMin = NONE;
        return move;
    }
} // namespace

void test_the_off_document_holds_nothing()
{
    StaticJsonDocument<64> doc;
    writeOff(doc.to<JsonObject>());
    std::string text;
    serializeJson(doc, text);
    TEST_ASSERT_EQUAL_STRING("{\"enabled\":false}", text.c_str());
}

void test_the_live_document_while_moving()
{
    SystemLive system;
    system.stepUs = 412;
    system.maxStepUs = 980;
    system.passMs = 3;
    system.maxPassMs = 41;
    system.freeHeap = 101234;
    system.rssi = -61;
    system.uptimeS = 12345;
    system.resetReason = "power-on";
    const DynamicJsonDocument doc = live(movingRoof(), system);
    TEST_ASSERT_TRUE(doc["enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("ready", doc["log"]);
    JsonObjectConst roof = doc["roof"];
    TEST_ASSERT_EQUAL_STRING("opening", roof["state"]);
    TEST_ASSERT_EQUAL_STRING("open", roof["target"]);
    TEST_ASSERT_EQUAL_STRING("cruise", roof["phase"]);
    TEST_ASSERT_EQUAL_STRING("forward", roof["direction"]);
    TEST_ASSERT_EQUAL_STRING("between", roof["hall"]);
    TEST_ASSERT_EQUAL_INT(100, roof["dutyPercent"]);
    TEST_ASSERT_TRUE(roof["en"].as<bool>());
    TEST_ASSERT_EQUAL_INT(255, roof["rpwm"]);
    TEST_ASSERT_EQUAL_INT(0, roof["lpwm"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, roof["powerWatts"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, roof["sagVolts"].as<float>());
    TEST_ASSERT_EQUAL_UINT32(20900, roof["driveBudgetUsedMs"]);
    TEST_ASSERT_TRUE(roof["remainingMs"].as<int>() > 0);
    TEST_ASSERT_EQUAL_INT(79, doc["control"]["watchdogMarginMs"]);
    TEST_ASSERT_EQUAL_INT(412, doc["control"]["stepUs"]);
    TEST_ASSERT_EQUAL_INT(-61, doc["wifi"]["rssi"]);
    TEST_ASSERT_EQUAL_STRING("power-on", doc["resetReason"]);
    TEST_ASSERT_EQUAL_UINT32(12, doc["move"]["id"]);
    TEST_ASSERT_TRUE(doc["move"]["active"].as<bool>());
}

void test_unreadable_values_are_null()
{
    RoofLive roof = movingRoof();
    roof.settings.currentSenseEnabled = false;
    roof.supplyVolts = NAN;
    roof.status.positionKnown = false;
    roof.log.sagVolts = NAN;
    SystemLive system; // the demo: no device values
    system.simulation = "demo";
    const DynamicJsonDocument doc = live(roof, system);
    JsonObjectConst r = doc["roof"];
    for (const char *key : {"motorAmps", "powerWatts", "supplyVolts", "sagVolts", "positionMm", "remainingMs"})
        TEST_ASSERT_TRUE_MESSAGE(r[key].isNull(), key);
    TEST_ASSERT_TRUE(doc["control"]["stepUs"].isNull());
    TEST_ASSERT_TRUE(doc["loop"]["passMs"].isNull());
    TEST_ASSERT_TRUE(doc["memory"]["freeHeap"].isNull());
    TEST_ASSERT_TRUE(doc["memory"]["stackFree"]["control"].isNull());
    TEST_ASSERT_TRUE(doc["wifi"]["rssi"].isNull());
    TEST_ASSERT_TRUE(doc["resetReason"].isNull());
    TEST_ASSERT_EQUAL_STRING("demo", doc["simulation"]);
}

void test_at_rest_the_move_times_are_null_and_the_budget_is_the_larger()
{
    RoofLive roof = movingRoof();
    roof.status.state = Roof::State::Open;
    roof.status.driving = false;
    roof.log.moveActive = false;
    const DynamicJsonDocument doc = live(roof, SystemLive());
    JsonObjectConst r = doc["roof"];
    TEST_ASSERT_TRUE(r["target"].isNull());
    TEST_ASSERT_TRUE(r["elapsedMs"].isNull());
    TEST_ASSERT_TRUE(r["timeoutMs"].isNull());
    TEST_ASSERT_TRUE(r["remainingMs"].isNull());
    TEST_ASSERT_EQUAL_UINT32(20900, r["driveBudgetUsedMs"]);
    TEST_ASSERT_FALSE(doc["move"]["active"].as<bool>());
}

void test_an_unavailable_log_has_no_move()
{
    SystemLive system;
    system.logReady = false;
    const DynamicJsonDocument doc = live(movingRoof(), system);
    TEST_ASSERT_EQUAL_STRING("unavailable", doc["log"]);
    TEST_ASSERT_TRUE(doc["move"].isNull());
}

void test_the_remaining_time_follows_the_estimate()
{
    RoofLive roof = movingRoof();
    roof.status.positionMm = static_cast<float>(roof.settings.travelMm);
    TEST_ASSERT_EQUAL_INT64(0, remainingMs(roof.status, roof.settings));
    roof.status.target = Roof::End::Closed;
    roof.status.state = Roof::State::Closing;
    TEST_ASSERT_TRUE(remainingMs(roof.status, roof.settings) > 40000);
}

void test_the_list_names_and_nulls()
{
    SummaryList list;
    Summary a;
    a.id = 2;
    a.startMs = 10000;
    a.endMs = 60500;
    a.origin = Roof::Origin::Client;
    a.result = Result::Fault;
    a.fault = Roof::FaultKind::Timeout;
    a.faultMeasured = 65000.0f;
    a.faultLimit = 64000.0f;
    a.restVolts = 12.6f;
    a.minVolts = 12.0f;
    list.add(a);
    Summary k;
    k.id = 1;
    k.kept = true;
    k.endMs = 50000;
    k.startEpoch = 1762800000;
    list.add(k);
    HeldIds held;
    held.ids[0] = 2;
    held.count = 1;
    const Clock clock{70500, 1762900000};
    const std::string text = whole([&](size_t i, std::string &out) { return listPart(list, held, true, clock, i, out); });
    DynamicJsonDocument doc(8192);
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, text), text.c_str());
    TEST_ASSERT_TRUE(doc["keep"].as<bool>());
    TEST_ASSERT_EQUAL_INT(5, doc["maxMoves"]);
    JsonArrayConst moves = doc["moves"];
    TEST_ASSERT_EQUAL_size_t(2, moves.size());
    JsonObjectConst kept = moves[0];
    TEST_ASSERT_EQUAL_UINT32(1, kept["id"]);
    TEST_ASSERT_TRUE(kept["kept"].as<bool>());
    TEST_ASSERT_FALSE(kept["profile"].as<bool>());
    TEST_ASSERT_TRUE(kept["startMs"].isNull());
    TEST_ASSERT_EQUAL_UINT32(1762800000, kept["startEpoch"]);
    TEST_ASSERT_EQUAL_UINT32(50000, kept["durationMs"]);
    JsonObjectConst fault = moves[1];
    TEST_ASSERT_TRUE(fault["profile"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("client", fault["origin"]);
    TEST_ASSERT_EQUAL_STRING("fault", fault["result"]);
    TEST_ASSERT_EQUAL_STRING("timeout", fault["fault"]["kind"]);
    TEST_ASSERT_EQUAL_UINT32(1762900000 - 60, fault["startEpoch"]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, fault["sagVolts"].as<float>());
    TEST_ASSERT_TRUE(fault["peakAmps"].isNull());
    TEST_ASSERT_TRUE(fault["stallAmps"].isNull());
}

void test_an_empty_list_is_valid_json()
{
    SummaryList list;
    const std::string text = whole([&](size_t i, std::string &out) { return listPart(list, HeldIds(), false, Clock{}, i, out); });
    DynamicJsonDocument doc(1024);
    TEST_ASSERT_FALSE(deserializeJson(doc, text));
    TEST_ASSERT_EQUAL_size_t(0, doc["moves"].size());
}

void test_a_move_document_has_its_points_in_physical_units()
{
    const Move move = sampleMove();
    const std::string text = whole([&](size_t i, std::string &out) { return movePart(move, Clock{}, i, out); });
    DynamicJsonDocument doc(8192);
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, text), text.c_str());
    TEST_ASSERT_EQUAL_UINT32(7, doc["move"]["id"]);
    TEST_ASSERT_EQUAL_STRING("arrived", doc["move"]["result"]);
    TEST_ASSERT_EQUAL_UINT32(250, doc["intervalMs"]);
    TEST_ASSERT_EQUAL_STRING("voltsMin", doc["columns"][3]);
    JsonArrayConst points = doc["points"];
    TEST_ASSERT_EQUAL_size_t(10, points.size());
    TEST_ASSERT_EQUAL_UINT32(500, points[2][0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.02f, points[2][1].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.02f, points[2][2].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, points[2][3].as<float>());
    TEST_ASSERT_EQUAL_INT(100, points[2][4]);
    TEST_ASSERT_TRUE(points[3][1].isNull());
    TEST_ASSERT_TRUE(points[3][3].isNull());
}

void test_a_move_with_no_points_is_valid_json()
{
    Move move = sampleMove();
    move.count = 0;
    const std::string text = whole([&](size_t i, std::string &out) { return movePart(move, Clock{}, i, out); });
    DynamicJsonDocument doc(2048);
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, text), text.c_str());
    TEST_ASSERT_EQUAL_size_t(0, doc["points"].size());
}

void test_the_csv_has_a_header_and_a_row_per_point()
{
    const Move move = sampleMove();
    const std::string text = whole([&](size_t i, std::string &out) { return csvPart(move, i, out); });
    size_t rows = 0;
    for (char c : text)
        rows += c == '\n';
    TEST_ASSERT_EQUAL_size_t(11, rows);
    TEST_ASSERT_EQUAL_INT(0, text.find("t_ms,amps_avg,amps_max,volts_min,duty_percent\r\n"));
    TEST_ASSERT_TRUE(text.find("750,,2.03,,100\r\n") != std::string::npos);
    TEST_ASSERT_TRUE(text.find("0,1.00,2.00,12.00,100\r\n") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_off_document_holds_nothing);
    RUN_TEST(test_the_live_document_while_moving);
    RUN_TEST(test_unreadable_values_are_null);
    RUN_TEST(test_at_rest_the_move_times_are_null_and_the_budget_is_the_larger);
    RUN_TEST(test_an_unavailable_log_has_no_move);
    RUN_TEST(test_the_remaining_time_follows_the_estimate);
    RUN_TEST(test_the_list_names_and_nulls);
    RUN_TEST(test_an_empty_list_is_valid_json);
    RUN_TEST(test_a_move_document_has_its_points_in_physical_units);
    RUN_TEST(test_a_move_with_no_points_is_valid_json);
    RUN_TEST(test_the_csv_has_a_header_and_a_row_per_point);
    return UNITY_END();
}
