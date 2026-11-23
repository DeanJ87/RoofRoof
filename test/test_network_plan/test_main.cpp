// The network mode on a board with Ethernet (spec 023 FR-022 to FR-024,
// research R9): when the Wi-Fi station starts and when the setup hotspot opens.

#include <unity.h>

#include "NetworkPlan.h"

using namespace SQM;
using namespace SQM::Net;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // Auto, a saved network, no cable since boot.
    PlanInputs down(uint32_t ms)
    {
        PlanInputs in;
        in.hasCredentials = true;
        in.msEthernetDown = ms;
        in.msSinceBoot = ms;
        return in;
    }

    constexpr uint32_t HOTSPOT_MS = CaptivePortal::FALLBACK_AFTER_MS;
} // namespace

void test_auto_falls_back_to_wifi_after_30_s_without_an_address(void)
{
    TEST_ASSERT_FALSE(wifiPlan(down(0)).startStation);
    TEST_ASSERT_FALSE(wifiPlan(down(ETHERNET_FALLBACK_MS - 1)).startStation);
    TEST_ASSERT_TRUE(wifiPlan(down(ETHERNET_FALLBACK_MS)).startStation);
}

void test_auto_never_starts_wifi_while_ethernet_is_up(void)
{
    PlanInputs in = down(ETHERNET_FALLBACK_MS * 10);
    in.ethernetUp = true;
    TEST_ASSERT_FALSE(wifiPlan(in).startStation);
}

void test_auto_needs_a_saved_network_and_starts_once(void)
{
    PlanInputs in = down(ETHERNET_FALLBACK_MS);
    in.hasCredentials = false; // empty Wi-Fi settings are normal on Ethernet
    TEST_ASSERT_FALSE(wifiPlan(in).startStation);
    in.hasCredentials = true;
    in.stationStarted = true;
    TEST_ASSERT_FALSE(wifiPlan(in).startStation);
}

void test_ethernet_only_never_starts_wifi_or_the_hotspot(void)
{
    PlanInputs in = down(HOTSPOT_MS * 100);
    in.mode = NetMode::Ethernet;
    const Plan plan = wifiPlan(in);
    TEST_ASSERT_FALSE(plan.startStation);
    TEST_ASSERT_FALSE(plan.openHotspot);
    in.hasCredentials = false;
    TEST_ASSERT_FALSE(wifiPlan(in).openHotspot);
}

void test_wifi_only_leaves_it_to_the_wifi_rules(void)
{
    PlanInputs in = down(HOTSPOT_MS * 100);
    in.mode = NetMode::Wifi;
    const Plan plan = wifiPlan(in);
    TEST_ASSERT_FALSE(plan.startStation);
    TEST_ASSERT_FALSE(plan.openHotspot);
}

void test_hotspot_after_45_s_with_no_link_and_no_network(void)
{
    PlanInputs in = down(HOTSPOT_MS - 1);
    in.hasCredentials = false;
    TEST_ASSERT_FALSE(wifiPlan(in).openHotspot);
    in.msSinceBoot = HOTSPOT_MS;
    TEST_ASSERT_TRUE(wifiPlan(in).openHotspot);
}

void test_a_link_gets_20_s_for_its_address(void)
{
    PlanInputs in = down(HOTSPOT_MS + 5000);
    in.ethernetLink = true;
    in.msSinceLink = ADDRESS_WAIT_MS - 1;
    TEST_ASSERT_FALSE(wifiPlan(in).openHotspot);
    in.msSinceLink = ADDRESS_WAIT_MS;
    TEST_ASSERT_TRUE(wifiPlan(in).openHotspot);
}

void test_hotspot_never_after_a_join_or_twice(void)
{
    PlanInputs in = down(HOTSPOT_MS * 4);
    in.joinedSinceBoot = true; // a later outage is retried, not answered with a hotspot
    TEST_ASSERT_FALSE(wifiPlan(in).openHotspot);
    in.joinedSinceBoot = false;
    in.hotspotOpen = true;
    TEST_ASSERT_FALSE(wifiPlan(in).openHotspot);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_auto_falls_back_to_wifi_after_30_s_without_an_address);
    RUN_TEST(test_auto_never_starts_wifi_while_ethernet_is_up);
    RUN_TEST(test_auto_needs_a_saved_network_and_starts_once);
    RUN_TEST(test_ethernet_only_never_starts_wifi_or_the_hotspot);
    RUN_TEST(test_wifi_only_leaves_it_to_the_wifi_rules);
    RUN_TEST(test_hotspot_after_45_s_with_no_link_and_no_network);
    RUN_TEST(test_a_link_gets_20_s_for_its_address);
    RUN_TEST(test_hotspot_never_after_a_join_or_twice);
    return UNITY_END();
}
