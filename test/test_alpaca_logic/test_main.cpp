#include <cstring>
#include <unity.h>

#include "AlpacaDiscovery.h"
#include "AlpacaProtocol.h"
#include "AlpacaRouter.h"
#include "ClientWatch.h"

using namespace SQM::Alpaca;

void setUp(void) {}
void tearDown(void) {}

void test_alpaca_double_parsing(void)
{
    double v = -1.0;
    TEST_ASSERT_TRUE(parseAlpacaDouble("0", v));
    TEST_ASSERT_EQUAL_FLOAT(0.0, v);
    TEST_ASSERT_TRUE(parseAlpacaDouble("1.5", v));
    TEST_ASSERT_EQUAL_FLOAT(1.5, v);
    TEST_ASSERT_FALSE(parseAlpacaDouble("", v));
    TEST_ASSERT_FALSE(parseAlpacaDouble("1.5x", v));
    TEST_ASSERT_FALSE(parseAlpacaDouble("nan", v));
}

// --- AlpacaDiscovery ---

void test_discovery_valid_packet(void)
{
    const char *payload = "alpacadiscovery1";
    TEST_ASSERT_TRUE(isValidDiscoveryRequest(reinterpret_cast<const uint8_t *>(payload), strlen(payload)));
}

void test_discovery_rejects_wrong_payload(void)
{
    const char *payload = "not-alpaca-at-all";
    TEST_ASSERT_FALSE(isValidDiscoveryRequest(reinterpret_cast<const uint8_t *>(payload), strlen(payload)));
}

void test_discovery_rejects_short_payload(void)
{
    const char *payload = "alpaca";
    TEST_ASSERT_FALSE(isValidDiscoveryRequest(reinterpret_cast<const uint8_t *>(payload), strlen(payload)));
}

void test_discovery_rejects_null(void)
{
    TEST_ASSERT_FALSE(isValidDiscoveryRequest(nullptr, 0));
}

void test_discovery_response_body(void)
{
    std::string body = buildDiscoveryResponse(80);
    TEST_ASSERT_EQUAL_STRING("{\"AlpacaPort\":80}", body.c_str());
}

// --- AlpacaProtocol ---

void test_param_name_case_insensitive(void)
{
    TEST_ASSERT_TRUE(paramNameEquals("ClientTransactionID", "clienttransactionid"));
    TEST_ASSERT_TRUE(paramNameEquals("CONNECTED", "Connected"));
    TEST_ASSERT_FALSE(paramNameEquals("ClientID", "ClientTransactionID"));
    TEST_ASSERT_FALSE(paramNameEquals("Connected", "Connecte"));
}

void test_put_param_names_are_case_sensitive(void)
{
    // GET query strings: any casing.
    TEST_ASSERT_TRUE(paramNameMatches("averageperiod", "AveragePeriod", false));
    TEST_ASSERT_TRUE(paramNameMatches("ClientTransactionID", "ClientTransactionID", false));
    // PUT form bodies: exact casing only (ConformU "Bad casing" checks).
    TEST_ASSERT_TRUE(paramNameMatches("AveragePeriod", "AveragePeriod", true));
    TEST_ASSERT_FALSE(paramNameMatches("averageperiod", "AveragePeriod", true));
    TEST_ASSERT_FALSE(paramNameMatches("clienttransactionid", "ClientTransactionID", true));
}

void test_client_transaction_id_parsing(void)
{
    TEST_ASSERT_EQUAL_UINT32(42, parseClientTransactionId("42"));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, parseClientTransactionId("4294967295"));
    TEST_ASSERT_EQUAL_UINT32(0, parseClientTransactionId("4294967296"));
    TEST_ASSERT_EQUAL_UINT32(0, parseClientTransactionId("-1"));
    TEST_ASSERT_EQUAL_UINT32(0, parseClientTransactionId("abc"));
    TEST_ASSERT_EQUAL_UINT32(0, parseClientTransactionId(""));
}

void test_alpaca_bool_parsing(void)
{
    bool value = false;
    TEST_ASSERT_TRUE(parseAlpacaBool("True", value));
    TEST_ASSERT_TRUE(value);
    TEST_ASSERT_TRUE(parseAlpacaBool("false", value));
    TEST_ASSERT_FALSE(value);
    TEST_ASSERT_FALSE(parseAlpacaBool("1", value));
    TEST_ASSERT_FALSE(parseAlpacaBool("", value));
}

void test_unique_id_includes_mac(void)
{
    TEST_ASSERT_EQUAL_STRING("roofroof-a1b2c3d4e5f6-dome-0", buildUniqueId(0xA1B2C3D4E5F6ULL, "dome", 0).c_str());
    // Upper 16 bits of the efuse value are ignored; short MACs are zero-padded.
    TEST_ASSERT_EQUAL_STRING("roofroof-000000000001-switch-1", buildUniqueId(0xFFFF000000000001ULL, "switch", 1).c_str());
}

// --- Router ---

namespace
{
    // Two stand-in devices: the router must work for whatever list the
    // Backend gives (spec 006 adds the real Dome).
    class FakeBackend : public SQM::Alpaca::Backend
    {
    public:
        bool enabled = true;
        std::vector<DeviceInfo> list = {
            {"Dome", "dome", 0, "Test dome", "A dome for the router tests", 3},
            {"Switch", "switch", 1, "Test switch", "A switch for the router tests", 3},
        };
        bool alpacaEnabled() const override { return enabled; }
        std::vector<DeviceInfo> devices() const override { return list; }
        std::string name = "Observatory";
        std::string where = "";
        std::string serverName() const override { return name; }
        std::string location() const override { return where; }
        std::string timestampUtc() const override { return ""; }
    };

    const ServerIdentity IDENTITY{"RoofRoof", "RoofRoof", "1.2.3", 0x1234};

    SQM::Alpaca::Response route(
        SQM::Alpaca::Router &router, bool put, const std::string &path, std::vector<std::pair<std::string, std::string>> params = {})
    {
        SQM::Alpaca::Request request;
        request.get = !put;
        request.put = put;
        request.path = path;
        request.params = std::move(params);
        SQM::Alpaca::Response response;
        TEST_ASSERT_TRUE(router.handle(request, response));
        return response;
    }
} // namespace

void test_router_common_methods_come_from_the_device_list(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    SQM::Alpaca::Response r = route(router, false, "/api/v1/dome/0/name", {{"clienttransactionid", "7"}});
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING(
        "{\"Value\":\"Test dome\",\"ClientTransactionID\":7,\"ServerTransactionID\":1,\"ErrorNumber\":0,\"ErrorMessage\":\"\"}",
        r.body.c_str());
    r = route(router, false, "/api/v1/switch/1/interfaceversion");
    TEST_ASSERT_NOT_NULL(strstr(r.body.c_str(), "\"Value\":3"));
    TEST_ASSERT_NOT_NULL(strstr(r.body.c_str(), "\"ServerTransactionID\":2"));
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/api/v1/dome/0/description").body.c_str(), "A dome for the router tests"));
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/api/v1/dome/0/supportedactions").body.c_str(), "\"Value\":[]"));
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/api/v1/dome/0/connecting").body.c_str(), "\"Value\":false"));
}

void test_router_configured_devices_lists_the_backend_devices(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    const std::string body = route(router, false, "/management/v1/configureddevices").body;
    TEST_ASSERT_NOT_NULL(strstr(body.c_str(), "\"DeviceType\":\"Dome\""));
    TEST_ASSERT_NOT_NULL(strstr(body.c_str(), "\"UniqueID\":\"roofroof-000000001234-dome-0\""));
    TEST_ASSERT_NOT_NULL(strstr(body.c_str(), "\"UniqueID\":\"roofroof-000000001234-switch-1\""));
    backend.list.clear();
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/management/v1/configureddevices").body.c_str(), "\"Value\":[]"));
}

// spec 001: with no devices, discovery and management still answer, and every
// device path is a plain 400.
void test_router_with_no_devices(void)
{
    FakeBackend backend;
    backend.list.clear();
    SQM::Alpaca::Router router(backend, IDENTITY);
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/management/apiversions").body.c_str(), "\"Value\":[1]"));
    TEST_ASSERT_EQUAL(400, route(router, false, "/api/v1/dome/0/name").status);
    TEST_ASSERT_FALSE(router.anyConnected());
}

void test_router_bad_requests_are_plain_400(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    TEST_ASSERT_EQUAL(400, route(router, false, "/api/v1/telescope/0/name").status);
    TEST_ASSERT_EQUAL(400, route(router, false, "/api/v1/dome/1/name").status);
    TEST_ASSERT_EQUAL(400, route(router, true, "/api/v1/dome/0/name").status);
    TEST_ASSERT_EQUAL(400, route(router, false, "/api/v1/dome/0/name/extra").status);
    // Device-specific methods arrive with the device (spec 006).
    const SQM::Alpaca::Response r = route(router, false, "/api/v1/dome/0/shutterstatus");
    TEST_ASSERT_EQUAL(400, r.status);
    TEST_ASSERT_EQUAL_STRING("text/plain", r.contentType);
    TEST_ASSERT_EQUAL(400, route(router, false, "/management/v1/bogus").status);
    SQM::Alpaca::Request other;
    other.get = true;
    other.path = "/api/status";
    SQM::Alpaca::Response unused;
    TEST_ASSERT_FALSE(router.handle(other, unused));
}

void test_router_put_parameter_casing(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    TEST_ASSERT_EQUAL(200, route(router, true, "/api/v1/dome/0/connected", {{"Connected", "True"}}).status);
    TEST_ASSERT_EQUAL(400, route(router, true, "/api/v1/dome/0/connected", {{"connected", "True"}}).status);
    const SQM::Alpaca::Response r = route(router, true, "/api/v1/dome/0/connect", {{"clienttransactionid", "9"}});
    TEST_ASSERT_NOT_NULL(strstr(r.body.c_str(), "\"ClientTransactionID\":0"));
}

void test_router_custom_actions_are_not_implemented(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    for (const char *method : {"action", "commandblind", "commandbool", "commandstring"})
    {
        const SQM::Alpaca::Response r = route(router, true, std::string("/api/v1/dome/0/") + method);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(r.body.c_str(), "\"ErrorNumber\":1024"), method);
    }
}

// D-20: with Alpaca off, nothing is served.
void test_router_connected_and_disabled(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    route(router, true, "/api/v1/switch/1/connected", {{"Connected", "True"}});
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/api/v1/switch/1/connected").body.c_str(), "\"Value\":true"));
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/api/v1/dome/0/connected").body.c_str(), "\"Value\":false"));
    backend.enabled = false;
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/api/v1/switch/1/connected").body.c_str(), "\"Value\":false"));
    TEST_ASSERT_NOT_NULL(strstr(route(router, true, "/api/v1/dome/0/connect").body.c_str(), "\"ErrorNumber\":1031"));
    TEST_ASSERT_NOT_NULL(strstr(route(router, false, "/management/v1/configureddevices").body.c_str(), "\"Value\":[]"));
}

// --- Client activity and ClientWatch (SQMeter spec 021) ---

void test_router_counts_requests_disconnects_and_client_id(void)
{
    FakeBackend backend;
    SQM::Alpaca::Router router(backend, IDENTITY);
    route(router, true, "/api/v1/dome/0/connected", {{"Connected", "True"}, {"ClientID", "42"}});
    route(router, false, "/api/v1/dome/0/name", {{"ClientID", "42"}});
    route(router, false, "/management/v1/description");              // management requests don't count
    route(router, false, "/api/v1/dome/0/name", {{"source", "ui"}}); // nor the web UI's own
    DeviceActivity sm = router.activity(0);
    TEST_ASSERT_TRUE(sm.connected);
    TEST_ASSERT_EQUAL_UINT32(2, sm.requests);
    TEST_ASSERT_EQUAL_UINT32(0, sm.disconnects);
    TEST_ASSERT_TRUE(sm.hasClientId);
    TEST_ASSERT_EQUAL_UINT32(42, sm.clientId);
    TEST_ASSERT_EQUAL_UINT32(0, router.activity(1).requests);

    route(router, true, "/api/v1/dome/0/disconnect");
    route(router, true, "/api/v1/dome/0/connected", {{"Connected", "False"}}); // already disconnected
    sm = router.activity(0);
    TEST_ASSERT_FALSE(sm.connected);
    TEST_ASSERT_EQUAL_UINT32(1, sm.disconnects);

    route(router, true, "/api/v1/switch/1/connect");
    TEST_ASSERT_TRUE(router.anyConnected());
    router.resetConnections();
    TEST_ASSERT_FALSE(router.anyConnected());
    TEST_ASSERT_EQUAL_UINT32(0, router.activity(1).disconnects);
}

namespace
{
    struct WatchRig
    {
        FakeBackend backend;
        SQM::Alpaca::Router router{backend, IDENTITY};
        ClientWatch watch;
        uint32_t silence[MAX_DEVICES] = {120000, 600000};
        bool enabled = true;

        void tick(uint32_t nowMs)
        {
            const DeviceActivity activity[MAX_DEVICES] = {router.activity(0), router.activity(1)};
            watch.update(activity, silence, enabled, nowMs);
        }
        const ClientState &sm() const { return watch.state(0); }
        const ClientState &oc() const { return watch.state(1); }
        void poll(const char *device = "dome/0") { route(router, false, std::string("/api/v1/") + device + "/name"); }
        void connect(const char *device = "dome/0")
        {
            route(router, true, std::string("/api/v1/") + device + "/connected", {{"Connected", "True"}});
        }
        void disconnect(const char *device = "dome/0")
        {
            route(router, true, std::string("/api/v1/") + device + "/connected", {{"Connected", "False"}});
        }
    };
} // namespace

void test_watch_silence_and_recovery(void)
{
    WatchRig rig;
    rig.tick(1000);
    TEST_ASSERT_FALSE(rig.sm().watching); // nothing since the restart
    rig.connect();
    rig.tick(2000);
    TEST_ASSERT_TRUE(rig.sm().watching);
    rig.poll();
    rig.tick(5000);
    rig.tick(5000 + 120000);
    TEST_ASSERT_FALSE(rig.sm().silent); // exactly the silence time: not yet
    rig.tick(5000 + 120001);
    TEST_ASSERT_TRUE(rig.sm().silent);
    TEST_ASSERT_TRUE(rig.sm().connected); // silence doesn't disconnect
    rig.poll();
    rig.tick(200000);
    TEST_ASSERT_FALSE(rig.sm().silent);
    TEST_ASSERT_EQUAL_UINT32(200000, rig.sm().lastRequestMs);
}

void test_watch_clean_disconnect_is_not_silence(void)
{
    WatchRig rig;
    rig.tick(0);
    rig.connect();
    rig.tick(1000);
    rig.disconnect();
    rig.tick(2000);
    TEST_ASSERT_TRUE(rig.sm().disconnectedNow);
    TEST_ASSERT_FALSE(rig.sm().watching);
    rig.tick(3000);
    TEST_ASSERT_FALSE(rig.sm().disconnectedNow); // an edge, once
    // A tool polling after the session ended doesn't restart watching...
    rig.poll();
    rig.tick(4000);
    rig.tick(4000 + 600000);
    TEST_ASSERT_FALSE(rig.sm().watching);
    TEST_ASSERT_FALSE(rig.sm().silent);
    // ...a new connect does.
    rig.connect();
    rig.tick(700000);
    TEST_ASSERT_TRUE(rig.sm().watching);
}

void test_watch_after_restart_polling_counts(void)
{
    // The device restarted; the client keeps polling without connecting.
    WatchRig rig;
    rig.tick(500);
    rig.poll();
    rig.tick(1000);
    TEST_ASSERT_TRUE(rig.sm().watching);
    TEST_ASSERT_FALSE(rig.sm().connected);
    rig.tick(1000 + 120001);
    TEST_ASSERT_TRUE(rig.sm().silent);
}

void test_watch_devices_and_clients_are_separate(void)
{
    WatchRig rig;
    rig.tick(0);
    rig.connect("dome/0");
    rig.connect("switch/1");
    rig.tick(1000);
    // Only the first device is polled; each has its own silence time.
    for (uint32_t t = 2000; t < 600000; t += 5000)
    {
        rig.poll("dome/0");
        rig.tick(t);
    }
    TEST_ASSERT_FALSE(rig.sm().silent);
    TEST_ASSERT_FALSE(rig.oc().silent); // the second has 10 minutes
    rig.poll("dome/0");
    rig.tick(1000 + 600001);
    TEST_ASSERT_TRUE(rig.oc().silent);
    TEST_ASSERT_FALSE(rig.sm().silent);
}

void test_watch_off_when_alpaca_disabled(void)
{
    WatchRig rig;
    rig.tick(0);
    rig.connect();
    rig.tick(1000);
    rig.enabled = false;
    rig.tick(500000);
    TEST_ASSERT_FALSE(rig.sm().watching);
    TEST_ASSERT_FALSE(rig.sm().silent);
}

void test_watch_reset_forgets_everything(void)
{
    WatchRig rig;
    rig.tick(0);
    rig.connect();
    rig.tick(1000);
    rig.router.resetConnections();
    rig.watch.reset();
    rig.tick(2000);
    TEST_ASSERT_FALSE(rig.sm().watching);
    rig.tick(500000);
    TEST_ASSERT_FALSE(rig.sm().silent);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_alpaca_double_parsing);

    RUN_TEST(test_discovery_valid_packet);
    RUN_TEST(test_discovery_rejects_wrong_payload);
    RUN_TEST(test_discovery_rejects_short_payload);
    RUN_TEST(test_discovery_rejects_null);
    RUN_TEST(test_discovery_response_body);

    RUN_TEST(test_param_name_case_insensitive);
    RUN_TEST(test_put_param_names_are_case_sensitive);
    RUN_TEST(test_client_transaction_id_parsing);
    RUN_TEST(test_alpaca_bool_parsing);
    RUN_TEST(test_unique_id_includes_mac);

    RUN_TEST(test_router_common_methods_come_from_the_device_list);
    RUN_TEST(test_router_configured_devices_lists_the_backend_devices);
    RUN_TEST(test_router_with_no_devices);
    RUN_TEST(test_router_bad_requests_are_plain_400);
    RUN_TEST(test_router_put_parameter_casing);
    RUN_TEST(test_router_custom_actions_are_not_implemented);
    RUN_TEST(test_router_connected_and_disabled);

    RUN_TEST(test_router_counts_requests_disconnects_and_client_id);
    RUN_TEST(test_watch_silence_and_recovery);
    RUN_TEST(test_watch_clean_disconnect_is_not_silence);
    RUN_TEST(test_watch_after_restart_polling_counts);
    RUN_TEST(test_watch_devices_and_clients_are_separate);
    RUN_TEST(test_watch_off_when_alpaca_disabled);
    RUN_TEST(test_watch_reset_forgets_everything);

    return UNITY_END();
}
