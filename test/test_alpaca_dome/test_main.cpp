// The Dome's Alpaca members (spec 006, contracts/alpaca-dome.md), through the
// router with a stub backend that records what reached the roof.

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "AlpacaProtocol.h"
#include "AlpacaRouter.h"
#include "ClientWatch.h"

using namespace SQM::Alpaca;

namespace
{
    class DomeBackend : public Backend
    {
    public:
        bool enabled = true;
        std::string clock = "2026-11-11T22:00:00Z";
        DomeStatus status;
        DomeAnswer answer;
        std::vector<DomeAction> actions; // what reached the roof

        bool alpacaEnabled() const override { return enabled; }
        std::vector<DeviceInfo> devices() const override { return {domeDeviceInfo()}; }
        std::string serverName() const override { return "Observatory"; }
        std::string location() const override { return ""; }
        std::string timestampUtc() const override { return clock; }
        DomeStatus domeStatus() const override { return status; }
        DomeAnswer domeCommand(DomeAction action) override
        {
            actions.push_back(action);
            return answer;
        }
    };

    const ServerIdentity IDENTITY{"RoofRoof", "RoofRoof", "1.2.3", 0xA1B2C3D4E5F6ULL};
    const std::string BASE = "/api/v1/dome/0/";

    struct Rig
    {
        DomeBackend backend;
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
        Response get(const std::string &method) { return call(false, method); }
        Response put(const std::string &method, std::vector<std::pair<std::string, std::string>> params = {})
        {
            return call(true, method, std::move(params));
        }
        void connect() { TEST_ASSERT_EQUAL(0, errorOf(put("connected", {{"Connected", "true"}}))); }

        static int errorOf(const Response &r)
        {
            const char *at = strstr(r.body.c_str(), "\"ErrorNumber\":");
            TEST_ASSERT_NOT_NULL(at);
            return atoi(at + strlen("\"ErrorNumber\":"));
        }
    };

    bool has(const Response &r, const char *text)
    {
        return strstr(r.body.c_str(), text) != nullptr;
    }
} // namespace

void setUp(void) {}
void tearDown(void) {}

// --- The device ---

void test_lists_one_dome_with_a_stable_unique_id(void)
{
    Rig rig;
    Request request;
    request.get = true;
    request.path = "/management/v1/configureddevices";
    Response r;
    TEST_ASSERT_TRUE(rig.router.handle(request, r));
    TEST_ASSERT_TRUE(has(r, "\"DeviceName\":\"RoofRoof roof\""));
    TEST_ASSERT_TRUE(has(r, "\"DeviceType\":\"Dome\""));
    TEST_ASSERT_TRUE(has(r, "\"DeviceNumber\":0"));
    TEST_ASSERT_TRUE(has(r, "\"UniqueID\":\"roofroof-a1b2c3d4e5f6-dome-0\""));
    request.path = "/api/v1/dome/1/name"; // only Dome 0
    TEST_ASSERT_TRUE(rig.router.handle(request, r));
    TEST_ASSERT_EQUAL(400, r.status);
}

void test_lists_nothing_while_alpaca_is_off(void)
{
    Rig rig;
    rig.backend.enabled = false;
    Request request;
    request.get = true;
    request.path = "/management/v1/configureddevices";
    Response r;
    TEST_ASSERT_TRUE(rig.router.handle(request, r));
    TEST_ASSERT_TRUE(has(r, "\"Value\":[]"));
}

void test_name_description_and_interface_version(void)
{
    Rig rig;
    TEST_ASSERT_TRUE(has(rig.get("name"), "\"Value\":\"RoofRoof roof\""));
    TEST_ASSERT_TRUE(has(rig.get("description"), "\"Value\":\"Roll-off roof: shutter open, close and stop only\""));
    TEST_ASSERT_TRUE(has(rig.get("interfaceversion"), "\"Value\":3"));
}

// Spec 011 FR-026: bench and demo say so in the description; the name and
// the UniqueID's parts never change.
void test_simulated_roofs_say_so_in_the_description(void)
{
    const DeviceInfo standard = domeDeviceInfo();
    const DeviceInfo bench = domeDeviceInfo("bench");
    const DeviceInfo demo = domeDeviceInfo("demo");
    TEST_ASSERT_EQUAL_STRING("Roll-off roof: shutter open, close and stop only", standard.description.c_str());
    TEST_ASSERT_EQUAL_STRING(standard.description.c_str(), domeDeviceInfo("off").description.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "BENCH MODE (simulated roof, no motor): Roll-off roof: shutter open, close and stop only", bench.description.c_str());
    TEST_ASSERT_EQUAL_STRING("Demo (simulated roof): Roll-off roof: shutter open, close and stop only", demo.description.c_str());
    for (const DeviceInfo &info : {bench, demo})
    {
        TEST_ASSERT_EQUAL_STRING(standard.name.c_str(), info.name.c_str());
        TEST_ASSERT_EQUAL_STRING(standard.typeKey.c_str(), info.typeKey.c_str());
        TEST_ASSERT_EQUAL_UINT32(standard.number, info.number);
        TEST_ASSERT_EQUAL(standard.interfaceVersion, info.interfaceVersion);
    }
}

void test_only_cansetshutter_is_true(void)
{
    Rig rig;
    TEST_ASSERT_TRUE(has(rig.get("cansetshutter"), "\"Value\":true"));
    for (const char *m :
         {"canfindhome", "canpark", "cansetaltitude", "cansetazimuth", "cansetpark", "canslave", "cansyncazimuth", "slaved"})
    {
        const Response r = rig.get(m);
        TEST_ASSERT_EQUAL(200, r.status);
        TEST_ASSERT_TRUE_MESSAGE(has(r, "\"Value\":false"), m);
        TEST_ASSERT_EQUAL(0, Rig::errorOf(r));
    }
}

void test_missing_properties_are_not_implemented(void)
{
    Rig rig;
    for (const char *m : {"altitude", "azimuth", "athome", "atpark"})
        TEST_ASSERT_EQUAL_MESSAGE(ALPACA_ERR_NOT_IMPLEMENTED, Rig::errorOf(rig.get(m)), m);
}

void test_missing_methods_are_not_implemented_and_reach_nothing(void)
{
    Rig rig;
    rig.connect();
    for (const char *m : {"findhome", "park", "setpark", "slewtoaltitude", "slewtoazimuth", "synctoazimuth"})
        TEST_ASSERT_EQUAL_MESSAGE(ALPACA_ERR_NOT_IMPLEMENTED, Rig::errorOf(rig.put(m, {{"Azimuth", "10"}, {"Altitude", "10"}})), m);
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
}

void test_slaved_false_accepted_true_not_implemented(void)
{
    Rig rig;
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("slaved", {{"Slaved", "false"}})));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_IMPLEMENTED, Rig::errorOf(rig.put("slaved", {{"Slaved", "True"}})));
    TEST_ASSERT_EQUAL(400, rig.put("slaved").status);
    TEST_ASSERT_EQUAL(400, rig.put("slaved", {{"Slaved", "maybe"}}).status);
    TEST_ASSERT_EQUAL(400, rig.put("slaved", {{"slaved", "false"}}).status); // PUT names are exact
}

void test_get_of_an_action_is_a_bad_request(void)
{
    Rig rig;
    for (const char *m : {"openshutter", "closeshutter", "abortslew", "park"})
        TEST_ASSERT_EQUAL_MESSAGE(400, rig.get(m).status, m);
    TEST_ASSERT_EQUAL(400, rig.put("shutterstatus").status);
    TEST_ASSERT_EQUAL(400, rig.get("bogus").status);
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
}

// --- Shutter state ---

void test_shutter_status_and_slewing_come_from_the_backend(void)
{
    Rig rig;
    rig.backend.status = {ShutterState::Closing, true};
    TEST_ASSERT_TRUE(has(rig.get("shutterstatus"), "\"Value\":3"));
    TEST_ASSERT_TRUE(has(rig.get("slewing"), "\"Value\":true"));
    rig.backend.status = {ShutterState::Closed, false};
    TEST_ASSERT_TRUE(has(rig.get("shutterstatus"), "\"Value\":1"));
    TEST_ASSERT_TRUE(has(rig.get("slewing"), "\"Value\":false"));
}

void test_device_state_lists_shutter_slewing_and_time(void)
{
    Rig rig;
    rig.backend.status = {ShutterState::Opening, true};
    const Response r = rig.get("devicestate");
    TEST_ASSERT_TRUE(
        has(r,
            "\"Value\":[{\"Name\":\"ShutterStatus\",\"Value\":2},{\"Name\":\"Slewing\",\"Value\":true},"
            "{\"Name\":\"TimeStamp\",\"Value\":\"2026-11-11T22:00:00Z\"}]"));
    rig.backend.clock = "";
    TEST_ASSERT_TRUE(
        has(rig.get("devicestate"), "\"Value\":[{\"Name\":\"ShutterStatus\",\"Value\":2},{\"Name\":\"Slewing\",\"Value\":true}]"));
}

void test_alpaca_off_answers_disabled_for_the_state(void)
{
    Rig rig;
    rig.backend.enabled = false;
    rig.backend.clock = "";
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, Rig::errorOf(rig.get("shutterstatus")));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, Rig::errorOf(rig.get("slewing")));
    TEST_ASSERT_TRUE(has(rig.get("devicestate"), "\"Value\":[]"));
}

// --- Actions (constitution I-III through the backend) ---

void test_open_and_close_need_a_connection(void)
{
    Rig rig;
    for (const char *m : {"openshutter", "closeshutter"})
    {
        const Response r = rig.put(m);
        TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, Rig::errorOf(r));
        TEST_ASSERT_TRUE(has(r, "Not connected"));
    }
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
    // ...and again after a disconnect.
    rig.connect();
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("disconnect")));
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, Rig::errorOf(rig.put("openshutter")));
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
}

void test_connected_open_and_close_reach_the_roof(void)
{
    Rig rig;
    rig.connect();
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("openshutter")));
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("closeshutter")));
    TEST_ASSERT_EQUAL(2, rig.backend.actions.size());
    TEST_ASSERT_TRUE(rig.backend.actions[0] == DomeAction::OpenShutter);
    TEST_ASSERT_TRUE(rig.backend.actions[1] == DomeAction::CloseShutter);
}

void test_a_refusal_is_invalid_operation_with_its_reason(void)
{
    Rig rig;
    rig.connect();
    rig.backend.answer = {false, "Weather not safe: unsafe: rain"};
    Response r = rig.put("openshutter");
    TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_OPERATION, Rig::errorOf(r));
    TEST_ASSERT_EQUAL(0x40B, ALPACA_ERR_INVALID_OPERATION);
    TEST_ASSERT_TRUE(has(r, "\"ErrorMessage\":\"Weather not safe: unsafe: rain\""));
    rig.backend.answer = {false, "Mount not parked: park switch 1 not closed"};
    r = rig.put("closeshutter");
    TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_OPERATION, Rig::errorOf(r));
    TEST_ASSERT_TRUE(has(r, "Mount not parked: park switch 1 not closed"));
    // Never a silent refusal.
    rig.backend.answer = {false, ""};
    TEST_ASSERT_TRUE(has(rig.put("closeshutter"), "\"ErrorMessage\":\"Refused\""));
}

void test_abort_works_connected_or_not(void)
{
    Rig rig;
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("abortslew")));
    rig.connect();
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("abortslew")));
    TEST_ASSERT_EQUAL(2, rig.backend.actions.size());
    TEST_ASSERT_TRUE(rig.backend.actions[0] == DomeAction::AbortSlew);
    TEST_ASSERT_TRUE(rig.backend.actions[1] == DomeAction::AbortSlew);
}

void test_alpaca_off_refuses_open_and_close_without_reaching_the_roof(void)
{
    Rig rig;
    rig.connect();
    rig.backend.enabled = false;
    for (const char *m : {"openshutter", "closeshutter", "abortslew"})
    {
        const Response r = rig.put(m);
        TEST_ASSERT_EQUAL_MESSAGE(ALPACA_ERR_NOT_CONNECTED, Rig::errorOf(r), m);
        TEST_ASSERT_TRUE(has(r, "disabled"));
    }
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
}

// Alpaca off: the Dome changes nothing, a stop included (constitution VIII;
// the web UI's Stop is the person's).
void test_abort_with_alpaca_off_is_disabled(void)
{
    Rig rig;
    rig.backend.enabled = false;
    const Response r = rig.put("abortslew");
    TEST_ASSERT_EQUAL(ALPACA_ERR_NOT_CONNECTED, Rig::errorOf(r));
    TEST_ASSERT_TRUE(has(r, "disabled"));
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
}

void test_a_refused_abort_is_reported(void)
{
    Rig rig;
    rig.backend.answer = {false, "The roof is closing on unsafe weather: an imaging app can't stop it (Stop in the web UI can)"};
    const Response r = rig.put("abortslew");
    TEST_ASSERT_EQUAL(ALPACA_ERR_INVALID_OPERATION, Rig::errorOf(r));
    TEST_ASSERT_TRUE(has(r, "closing on unsafe weather"));
}

void test_disconnecting_never_reaches_the_roof(void)
{
    Rig rig;
    rig.connect();
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("connected", {{"Connected", "false"}})));
    rig.connect();
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("disconnect")));
    TEST_ASSERT_EQUAL(0, rig.backend.actions.size());
}

void test_requests_count_for_the_client_watch_except_the_ui_s(void)
{
    Rig rig;
    rig.get("shutterstatus");
    rig.put("abortslew", {{"ClientID", "42"}});
    TEST_ASSERT_EQUAL(2, rig.router.activity(0).requests);
    TEST_ASSERT_TRUE(rig.router.activity(0).hasClientId);
    TEST_ASSERT_EQUAL(42, rig.router.activity(0).clientId);
    rig.call(false, "devicestate", {{"source", "ui"}});
    TEST_ASSERT_EQUAL(2, rig.router.activity(0).requests);
}

// US5: the imaging-app watch follows the Dome's requests (device 0).
void test_client_watch_follows_the_dome(void)
{
    Rig rig;
    ClientWatch watch;
    const uint32_t silence[MAX_DEVICES] = {60000, 60000};
    DeviceActivity activity[MAX_DEVICES];
    auto pass = [&](uint32_t nowMs)
    {
        activity[0] = rig.router.activity(0);
        watch.update(activity, silence, true, nowMs);
    };
    pass(0);
    TEST_ASSERT_FALSE(watch.state(0).watching);
    rig.connect();
    rig.get("shutterstatus");
    pass(1000);
    TEST_ASSERT_TRUE(watch.state(0).connected);
    TEST_ASSERT_TRUE(watch.state(0).watching);
    pass(61000);
    TEST_ASSERT_FALSE(watch.state(0).silent); // 60 s since the last request: not yet
    pass(61001);
    TEST_ASSERT_TRUE(watch.state(0).silent);
    rig.get("slewing");
    pass(62000);
    TEST_ASSERT_FALSE(watch.state(0).silent);
    TEST_ASSERT_EQUAL(0, Rig::errorOf(rig.put("disconnect")));
    pass(63000);
    TEST_ASSERT_TRUE(watch.state(0).disconnectedNow);
    TEST_ASSERT_FALSE(watch.state(0).watching);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_lists_one_dome_with_a_stable_unique_id);
    RUN_TEST(test_lists_nothing_while_alpaca_is_off);
    RUN_TEST(test_name_description_and_interface_version);
    RUN_TEST(test_simulated_roofs_say_so_in_the_description);
    RUN_TEST(test_only_cansetshutter_is_true);
    RUN_TEST(test_missing_properties_are_not_implemented);
    RUN_TEST(test_missing_methods_are_not_implemented_and_reach_nothing);
    RUN_TEST(test_slaved_false_accepted_true_not_implemented);
    RUN_TEST(test_get_of_an_action_is_a_bad_request);
    RUN_TEST(test_shutter_status_and_slewing_come_from_the_backend);
    RUN_TEST(test_device_state_lists_shutter_slewing_and_time);
    RUN_TEST(test_alpaca_off_answers_disabled_for_the_state);
    RUN_TEST(test_open_and_close_need_a_connection);
    RUN_TEST(test_connected_open_and_close_reach_the_roof);
    RUN_TEST(test_a_refusal_is_invalid_operation_with_its_reason);
    RUN_TEST(test_abort_works_connected_or_not);
    RUN_TEST(test_alpaca_off_refuses_open_and_close_without_reaching_the_roof);
    RUN_TEST(test_abort_with_alpaca_off_is_disabled);
    RUN_TEST(test_a_refused_abort_is_reported);
    RUN_TEST(test_disconnecting_never_reaches_the_roof);
    RUN_TEST(test_requests_count_for_the_client_watch_except_the_ui_s);
    RUN_TEST(test_client_watch_follows_the_dome);
    return UNITY_END();
}
