// N.I.N.A. Advanced API v2 (lib/NinaLogic): the requests and every reading the
// replies can give. Constitution II: a reply that can't be read is never taken
// as "parked".

#include <unity.h>

#include "NinaApi.h"
#include "fixtures.h"

using namespace SQM::Nina;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const ApiV2 API;
    constexpr uint32_t NOW_MS = 123456;
} // namespace

void test_v2_info_request_is_get_mount_info(void)
{
    const Request request = API.mountInfoRequest();
    TEST_ASSERT_TRUE(request.method == Method::Get);
    TEST_ASSERT_EQUAL_STRING("/v2/api/equipment/mount/info", request.path.c_str());
    TEST_ASSERT_TRUE(request.username.empty());
    TEST_ASSERT_TRUE(request.password.empty());
}

void test_v2_park_request_is_get_mount_park(void)
{
    const Request request = API.parkRequest();
    TEST_ASSERT_TRUE(request.method == Method::Get);
    TEST_ASSERT_EQUAL_STRING("/v2/api/equipment/mount/park", request.path.c_str());
}

void test_parse_info_parked(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_PARKED, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Good);
    TEST_ASSERT_TRUE(reading.connected);
    TEST_ASSERT_TRUE(reading.atPark);
    TEST_ASSERT_FALSE(reading.slewing);
    TEST_ASSERT_EQUAL_UINT32(NOW_MS, reading.takenAtMs);
}

void test_parse_info_tracking_not_parked(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_TRACKING, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Good);
    TEST_ASSERT_TRUE(reading.connected);
    TEST_ASSERT_FALSE(reading.atPark);
    TEST_ASSERT_FALSE(reading.slewing);
}

void test_parse_info_slewing(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_SLEWING, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Good);
    TEST_ASSERT_TRUE(reading.slewing);
}

void test_parse_info_disconnected_is_good_and_not_connected(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_DISCONNECTED, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Good);
    TEST_ASSERT_FALSE(reading.connected);
    TEST_ASSERT_FALSE(reading.atPark);
}

void test_parse_info_api_error_keeps_ninas_text(void)
{
    const MountReading reading = API.parseMountInfo(500, Fixtures::INFO_UNKNOWN_ERROR, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::ApiError);
    TEST_ASSERT_EQUAL_STRING("Unknown error", reading.detail.c_str());
    TEST_ASSERT_FALSE(reading.atPark);
}

void test_parse_info_missing_at_park_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_MISSING_AT_PARK, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
    TEST_ASSERT_EQUAL_STRING("missing AtPark", reading.detail.c_str());
    TEST_ASSERT_FALSE(reading.atPark);
}

void test_parse_info_at_park_string_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_AT_PARK_STRING, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
    TEST_ASSERT_FALSE(reading.atPark);
}

void test_parse_info_missing_connected_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_MISSING_CONNECTED, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
    TEST_ASSERT_EQUAL_STRING("missing Connected", reading.detail.c_str());
}

void test_parse_info_response_not_object_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_RESPONSE_NOT_OBJECT, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
}

void test_parse_info_without_success_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_NO_SUCCESS, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
}

void test_parse_info_truncated_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, Fixtures::INFO_TRUNCATED, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
}

void test_parse_info_not_json_unreadable_with_status(void)
{
    const MountReading reading = API.parseMountInfo(404, Fixtures::NOT_JSON, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
    TEST_ASSERT_EQUAL_STRING("HTTP 404", reading.detail.c_str());
}

void test_parse_info_success_with_non_200_unreadable(void)
{
    // Trust neither the status line nor the body alone (research R2, not verified).
    const MountReading reading = API.parseMountInfo(202, Fixtures::INFO_PARKED, NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
    TEST_ASSERT_FALSE(reading.atPark);
}

void test_parse_info_empty_body_unreadable(void)
{
    const MountReading reading = API.parseMountInfo(200, "", NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreadable);
}

void test_parse_park_parking_accepted(void)
{
    const ParkReply reply = API.parsePark(200, Fixtures::PARK_PARKING);
    TEST_ASSERT_TRUE(reply.outcome == ParkOutcome::Accepted);
}

void test_parse_park_already_parked(void)
{
    const ParkReply reply = API.parsePark(200, Fixtures::PARK_ALREADY);
    TEST_ASSERT_TRUE(reply.outcome == ParkOutcome::AlreadyParked);
}

void test_parse_park_not_connected_refused(void)
{
    const ParkReply reply = API.parsePark(409, Fixtures::PARK_NOT_CONNECTED);
    TEST_ASSERT_TRUE(reply.outcome == ParkOutcome::Refused);
    TEST_ASSERT_EQUAL_STRING("Mount not connected", reply.detail.c_str());
}

void test_parse_park_unexpected_text_unreadable(void)
{
    const ParkReply reply = API.parsePark(200, Fixtures::PARK_UNEXPECTED_TEXT);
    TEST_ASSERT_TRUE(reply.outcome == ParkOutcome::Unreadable);
}

void test_parse_park_not_json_unreadable(void)
{
    const ParkReply reply = API.parsePark(500, Fixtures::NOT_JSON);
    TEST_ASSERT_TRUE(reply.outcome == ParkOutcome::Unreadable);
    TEST_ASSERT_EQUAL_STRING("HTTP 500", reply.detail.c_str());
}

void test_failed_reading_is_not_parked(void)
{
    const MountReading reading = failedReading(ReadingStatus::Unreachable, "connection refused", NOW_MS);
    TEST_ASSERT_TRUE(reading.status == ReadingStatus::Unreachable);
    TEST_ASSERT_FALSE(reading.atPark);
    TEST_ASSERT_FALSE(reading.connected);
    TEST_ASSERT_EQUAL_UINT32(NOW_MS, reading.takenAtMs);
    TEST_ASSERT_EQUAL_STRING("connection refused", reading.detail.c_str());
}

void test_make_api_v2(void)
{
    const auto api = makeApi(ApiVersion::V2);
    TEST_ASSERT_NOT_NULL(api.get());
    TEST_ASSERT_EQUAL_STRING("v2", api->name());
}

void test_parse_api_version(void)
{
    ApiVersion version = ApiVersion::V2;
    TEST_ASSERT_TRUE(parseApiVersion("v2", version));
    TEST_ASSERT_TRUE(version == ApiVersion::V2);
    TEST_ASSERT_FALSE(parseApiVersion("v3", version));
    TEST_ASSERT_FALSE(parseApiVersion("", version));
    TEST_ASSERT_EQUAL_STRING("v2", apiVersionName(ApiVersion::V2));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_v2_info_request_is_get_mount_info);
    RUN_TEST(test_v2_park_request_is_get_mount_park);
    RUN_TEST(test_parse_info_parked);
    RUN_TEST(test_parse_info_tracking_not_parked);
    RUN_TEST(test_parse_info_slewing);
    RUN_TEST(test_parse_info_disconnected_is_good_and_not_connected);
    RUN_TEST(test_parse_info_api_error_keeps_ninas_text);
    RUN_TEST(test_parse_info_missing_at_park_unreadable);
    RUN_TEST(test_parse_info_at_park_string_unreadable);
    RUN_TEST(test_parse_info_missing_connected_unreadable);
    RUN_TEST(test_parse_info_response_not_object_unreadable);
    RUN_TEST(test_parse_info_without_success_unreadable);
    RUN_TEST(test_parse_info_truncated_unreadable);
    RUN_TEST(test_parse_info_not_json_unreadable_with_status);
    RUN_TEST(test_parse_info_success_with_non_200_unreadable);
    RUN_TEST(test_parse_info_empty_body_unreadable);
    RUN_TEST(test_parse_park_parking_accepted);
    RUN_TEST(test_parse_park_already_parked);
    RUN_TEST(test_parse_park_not_connected_refused);
    RUN_TEST(test_parse_park_unexpected_text_unreadable);
    RUN_TEST(test_parse_park_not_json_unreadable);
    RUN_TEST(test_failed_reading_is_not_parked);
    RUN_TEST(test_make_api_v2);
    RUN_TEST(test_parse_api_version);
    return UNITY_END();
}
