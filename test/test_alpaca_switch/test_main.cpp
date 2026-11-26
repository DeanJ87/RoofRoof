// Spec 043: the Alpaca Switch (ISwitchV3, device 0), through the router with a
// stub backend: what ConformU checks of a read-only switch device, so a
// regression fails here before the CI conformance run.

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "AlpacaProtocol.h"
#include "AlpacaRouter.h"

using namespace SQM::Alpaca;

namespace
{
    class SwitchBackend : public Backend
    {
    public:
        bool enabled = true;
        std::vector<SwitchReading> readings{
            {"Mount may move", "True only with the roof at an end", true},
            {"Roof fully open", "Open magnet", false},
            {"Roof fully closed", "Closed magnet", true},
        };

        bool alpacaEnabled() const override { return enabled; }
        std::vector<DeviceInfo> devices() const override { return {domeDeviceInfo(), switchDeviceInfo()}; }
        std::string serverName() const override { return "Observatory"; }
        std::string location() const override { return ""; }
        std::string timestampUtc() const override { return "2026-11-26T22:00:00Z"; }
        DomeStatus domeStatus() const override { return DomeStatus{}; }
        DomeAnswer domeCommand(DomeAction) override { return DomeAnswer{}; }
        std::vector<SwitchReading> switches() const override { return readings; }
    };

    const ServerIdentity IDENTITY{"RoofRoof", "RoofRoof", "1.2.3", 0xA1B2C3D4E5F6ULL};
    const std::string BASE = "/api/v1/switch/0/";

    struct Rig
    {
        SwitchBackend backend;
        Router router{backend, IDENTITY};

        Response call(bool put, const std::string &method, std::vector<std::pair<std::string, std::string>> params = {})
        {
            Request request;
            request.get = !put;
            request.put = put;
            request.path = BASE + method;
            request.params = std::move(params);
            Response response;
            TEST_ASSERT_TRUE(router.handle(request, response));
            return response;
        }
        Response get(const std::string &method, const std::string &id = "")
        {
            return id.empty() ? call(false, method) : call(false, method, {{"Id", id}});
        }
        Response put(const std::string &method, std::vector<std::pair<std::string, std::string>> params)
        {
            return call(true, method, std::move(params));
        }
    };

    int errorOf(const Response &r)
    {
        const char *at = strstr(r.body.c_str(), "\"ErrorNumber\":");
        TEST_ASSERT_NOT_NULL(at);
        return atoi(at + strlen("\"ErrorNumber\":"));
    }

    bool has(const Response &r, const char *text)
    {
        return strstr(r.body.c_str(), text) != nullptr;
    }

    const char *const PER_SWITCH_GETS[] = {
        "canwrite",
        "canasync",
        "getswitch",
        "getswitchvalue",
        "getswitchname",
        "getswitchdescription",
        "minswitchvalue",
        "maxswitchvalue",
        "switchstep",
        "statechangecomplete",
    };
} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_listed_beside_the_dome_with_its_own_unique_id(void)
{
    Rig rig;
    Request request;
    request.get = true;
    request.path = "/management/v1/configureddevices";
    Response response;
    TEST_ASSERT_TRUE(rig.router.handle(request, response));
    TEST_ASSERT_TRUE(has(response, "\"DeviceType\":\"Dome\""));
    TEST_ASSERT_TRUE(has(response, "\"DeviceType\":\"Switch\""));
    TEST_ASSERT_TRUE(has(response, "roofroof-a1b2c3d4e5f6-dome-0"));
    TEST_ASSERT_TRUE(has(response, "roofroof-a1b2c3d4e5f6-switch-0"));
}

void test_common_members_and_interface_version_3(void)
{
    Rig rig;
    TEST_ASSERT_TRUE(has(rig.get("interfaceversion"), "\"Value\":3"));
    TEST_ASSERT_TRUE(has(rig.get("name"), "\"Value\":\"RoofRoof\""));
    TEST_ASSERT_TRUE(has(rig.get("description"), "read-only"));
    TEST_ASSERT_EQUAL(0, errorOf(rig.put("connected", {{"Connected", "true"}})));
    TEST_ASSERT_TRUE(has(rig.get("connected"), "\"Value\":true"));
}

void test_maxswitch_and_each_reading(void)
{
    Rig rig;
    TEST_ASSERT_TRUE(has(rig.get("maxswitch"), "\"Value\":3"));
    TEST_ASSERT_TRUE(has(rig.get("getswitch", "0"), "\"Value\":true"));
    TEST_ASSERT_TRUE(has(rig.get("getswitch", "1"), "\"Value\":false"));
    TEST_ASSERT_TRUE(has(rig.get("getswitchvalue", "0"), "\"Value\":1"));
    TEST_ASSERT_TRUE(has(rig.get("getswitchvalue", "1"), "\"Value\":0"));
    TEST_ASSERT_TRUE(has(rig.get("getswitchname", "0"), "\"Value\":\"Mount may move\""));
    TEST_ASSERT_TRUE(has(rig.get("getswitchname", "2"), "\"Value\":\"Roof fully closed\""));
    TEST_ASSERT_TRUE(has(rig.get("getswitchdescription", "0"), "at an end"));
    TEST_ASSERT_TRUE(has(rig.get("minswitchvalue", "0"), "\"Value\":0"));
    TEST_ASSERT_TRUE(has(rig.get("maxswitchvalue", "0"), "\"Value\":1"));
    TEST_ASSERT_TRUE(has(rig.get("switchstep", "0"), "\"Value\":1"));
    // Read-only and never asynchronous (ConformU: CanAsync needs CanWrite).
    TEST_ASSERT_TRUE(has(rig.get("canwrite", "0"), "\"Value\":false"));
    TEST_ASSERT_TRUE(has(rig.get("canasync", "0"), "\"Value\":false"));
    TEST_ASSERT_TRUE(has(rig.get("statechangecomplete", "0"), "\"Value\":true"));
    // The value follows the backend.
    rig.backend.readings[0].value = false;
    TEST_ASSERT_TRUE(has(rig.get("getswitch", "0"), "\"Value\":false"));
}

void test_an_id_out_of_range_is_invalid_value_for_every_member(void)
{
    Rig rig;
    for (const char *id : {"-1", "3", "100"})
    {
        for (const char *method : PER_SWITCH_GETS)
            TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.get(method, id)));
        TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.put("setswitch", {{"Id", id}, {"State", "false"}})));
        TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.put("setswitchvalue", {{"Id", id}, {"Value", "0"}})));
        TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.put("setswitchname", {{"Id", id}, {"Name", "x"}})));
        TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.put("setasync", {{"Id", id}, {"State", "true"}})));
        TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.put("setasyncvalue", {{"Id", id}, {"Value", "1"}})));
        TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_VALUE, errorOf(rig.put("cancelasync", {{"Id", id}})));
    }
}

void test_writing_renaming_and_async_are_not_implemented(void)
{
    Rig rig;
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("setswitch", {{"Id", "0"}, {"State", "false"}})));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("setswitchvalue", {{"Id", "0"}, {"Value", "0"}})));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("setswitchname", {{"Id", "0"}, {"Name", "Mount may move"}})));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("setasync", {{"Id", "0"}, {"State", "true"}})));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("setasyncvalue", {{"Id", "0"}, {"Value", "1"}})));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("cancelasync", {{"Id", "0"}})));
    // Nothing changed.
    TEST_ASSERT_TRUE(has(rig.get("getswitch", "0"), "\"Value\":true"));
}

void test_a_missing_or_malformed_parameter_is_http_400(void)
{
    Rig rig;
    TEST_ASSERT_EQUAL(400, rig.get("getswitch").status);
    TEST_ASSERT_EQUAL(400, rig.get("getswitch", "one").status);
    TEST_ASSERT_EQUAL(400, rig.get("getswitch", "1.5").status);
    TEST_ASSERT_EQUAL(400, rig.put("setswitch", {{"Id", "0"}}).status);
    TEST_ASSERT_EQUAL(400, rig.put("setswitch", {{"Id", "0"}, {"State", "maybe"}}).status);
    TEST_ASSERT_EQUAL(400, rig.put("setswitchvalue", {{"Id", "0"}, {"Value", "high"}}).status);
    // A PUT's names are case-sensitive: "id" is no Id.
    TEST_ASSERT_EQUAL(400, rig.put("setswitch", {{"id", "0"}, {"State", "true"}}).status);
    // A GET's are not.
    TEST_ASSERT_EQUAL(200, rig.call(false, "getswitch", {{"id", "0"}}).status);
    // Values are checked by their form only (every write is refused anyway).
    for (const char *bad : {"", "e3", "1e", "--1", "1.2.3", "0x1", "1 "})
        TEST_ASSERT_EQUAL(400, rig.put("setswitchvalue", {{"Id", "0"}, {"Value", bad}}).status);
    for (const char *good : {"0", "1", "-0.5", ".5", "1.", "1e3", "2.5E-2", "+1"})
        TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, errorOf(rig.put("setswitchvalue", {{"Id", "0"}, {"Value", good}})));
    // An unknown member.
    TEST_ASSERT_EQUAL(400, rig.get("shutterstatus").status);
}

void test_device_state_lists_every_switch(void)
{
    Rig rig;
    const Response r = rig.get("devicestate");
    for (const char *name : {"GetSwitch0", "GetSwitchValue0", "StateChangeComplete0", "GetSwitch2", "GetSwitchValue2", "TimeStamp"})
        TEST_ASSERT_TRUE(has(r, name));
    TEST_ASSERT_FALSE(has(r, "GetSwitch3"));
}

void test_alpaca_off_answers_the_disabled_error(void)
{
    Rig rig;
    rig.backend.enabled = false;
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, errorOf(rig.get("getswitch", "0")));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, errorOf(rig.put("setswitch", {{"Id", "0"}, {"State", "true"}})));
    TEST_ASSERT_FALSE(has(rig.get("devicestate"), "GetSwitch0"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_listed_beside_the_dome_with_its_own_unique_id);
    RUN_TEST(test_common_members_and_interface_version_3);
    RUN_TEST(test_maxswitch_and_each_reading);
    RUN_TEST(test_an_id_out_of_range_is_invalid_value_for_every_member);
    RUN_TEST(test_writing_renaming_and_async_are_not_implemented);
    RUN_TEST(test_a_missing_or_malformed_parameter_is_http_400);
    RUN_TEST(test_device_state_lists_every_switch);
    RUN_TEST(test_alpaca_off_answers_the_disabled_error);
    return UNITY_END();
}
