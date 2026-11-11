// Spec 016: the calibration run's status document and the start request
// (lib/CalibrationLogic, CalibrationDocument; contracts/calibration-api.md).

#include <unity.h>

#include "CalibrationDocument.h"
#include "../calibration_rig/CalibrationRig.h"

using namespace CalibTest;

void setUp(void) {}
void tearDown(void) {}

// --- The document -------------------------------------------------------------------------

void test_the_document_of_a_finished_run(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.finish());
    DynamicJsonDocument doc(DOCUMENT_CAPACITY);
    writeDocument(doc.to<JsonObject>(), r.rep(), r.nowMs);
    TEST_ASSERT_FALSE(doc.overflowed());
    TEST_ASSERT_EQUAL_STRING("done", doc["state"]);
    TEST_ASSERT_FALSE(doc["active"].as<bool>());
    TEST_ASSERT_TRUE(doc["leg"].isNull());
    TEST_ASSERT_EQUAL_STRING("closed", doc["request"]["at"]);
    TEST_ASSERT_EQUAL_UINT32(180, doc["request"]["limitSeconds"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("done", doc["ending"]["reason"]);
    TEST_ASSERT_EQUAL(2, doc["legs"].size());
    TEST_ASSERT_EQUAL_STRING("slowOpen", doc["legs"][0]["name"]);
    TEST_ASSERT_TRUE(doc["legs"][0]["ampsMean"].isNull());
    TEST_ASSERT_EQUAL_STRING("low", doc["hall"]["closed"]["level"]);
    TEST_ASSERT_EQUAL_UINT32(1850, doc["proposal"]["travelMm"]["old"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(1800, doc["proposal"]["travelMm"]["new"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("open", doc["proposal"]["hallHighEnd"]["new"]);
    TEST_ASSERT_TRUE(doc["proposal"]["hallProblem"].isNull());
    TEST_ASSERT_TRUE(doc["proposal"]["warnings"].is<JsonArray>());
}

void test_the_document_while_running_and_idle(void)
{
    Rig r;
    DynamicJsonDocument doc(DOCUMENT_CAPACITY);
    writeDocument(doc.to<JsonObject>(), r.rep(), r.nowMs);
    TEST_ASSERT_EQUAL_STRING("idle", doc["state"]);
    TEST_ASSERT_TRUE(doc["proposal"].isNull());
    TEST_ASSERT_TRUE(doc["request"].isNull());
    TEST_ASSERT_TRUE(r.start().accepted);
    TEST_ASSERT_TRUE(r.until(LegKind::SlowOpen));
    r.steps(2000);
    writeDocument(doc.to<JsonObject>(), r.rep(), r.nowMs);
    TEST_ASSERT_EQUAL_STRING("running", doc["state"]);
    TEST_ASSERT_EQUAL_STRING("slowOpen", doc["leg"]["name"]);
    TEST_ASSERT_EQUAL_STRING("drive", doc["leg"]["phase"]);
    TEST_ASSERT_EQUAL(1, doc["leg"]["index"].as<int>());
    TEST_ASSERT_EQUAL(2, doc["leg"]["count"].as<int>());
    TEST_ASSERT_EQUAL_UINT32(LIMIT_MS, doc["leg"]["limitMs"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["proposal"].isNull());
}

void test_the_start_request_is_parsed(void)
{
    StaticJsonDocument<256> doc;
    Request req;
    const char *error = nullptr;
    deserializeJson(doc, "{\"attended\":true,\"at\":\"partWay\",\"travelMm\":1800,\"topPass\":true}");
    TEST_ASSERT_TRUE(parseRequest(doc.as<JsonVariantConst>(), req, error));
    TEST_ASSERT_TRUE(req.attended && req.topPass && req.at == Where::PartWay && req.travelMm == 1800);
    deserializeJson(doc, "{\"attended\":false,\"at\":\"open\",\"travelMm\":900}");
    TEST_ASSERT_TRUE(parseRequest(doc.as<JsonVariantConst>(), req, error));
    TEST_ASSERT_FALSE(req.attended || req.topPass);
    for (const char *bad :
         {"[]",
          "{\"at\":\"open\",\"travelMm\":900}",
          "{\"attended\":true,\"at\":\"middle\",\"travelMm\":900}",
          "{\"attended\":true,\"at\":\"open\",\"travelMm\":-1}",
          "{\"attended\":true,\"at\":\"open\",\"travelMm\":\"9\"}",
          "{\"attended\":true,\"at\":\"open\",\"travelMm\":900,\"topPass\":1}"})
    {
        deserializeJson(doc, bad);
        TEST_ASSERT_FALSE_MESSAGE(parseRequest(doc.as<JsonVariantConst>(), req, error), bad);
        TEST_ASSERT_NOT_NULL(error);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_document_of_a_finished_run);
    RUN_TEST(test_the_document_while_running_and_idle);
    RUN_TEST(test_the_start_request_is_parsed);
    return UNITY_END();
}
