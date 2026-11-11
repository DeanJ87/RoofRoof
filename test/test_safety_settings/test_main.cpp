// Which safety records a settings save restarts (lib/SafetyInputLogic
// SafetySettings, spec 004 T029): a reply to an old URL or topic must never
// count for a new one, and an unrelated save keeps every reading.

#include <unity.h>

#include "SafetySettings.h"

using namespace SQM::Safety;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    SourceSettings base()
    {
        SourceSettings s;
        s.httpEnabled = true;
        s.httpUrl = "http://sqmeter/api/safety";
        s.mqttEnabled = true;
        s.mqttTopic = "sqmeter";
        s.staleSeconds = 30;
        s.ninaUrl = "http://nina:1888";
        return s;
    }

    bool none(const SettingsChange &c)
    {
        return !c.sqmeterHttp && !c.sqmeterMqtt && !c.nina && !c.window;
    }
} // namespace

void test_first_call_restarts_everything(void)
{
    const SettingsChange c = settingsChange(nullptr, base());
    TEST_ASSERT_TRUE(c.sqmeterHttp && c.sqmeterMqtt && c.nina && c.window);
}

void test_unrelated_save_restarts_nothing(void)
{
    const SourceSettings s = base();
    TEST_ASSERT_TRUE(none(settingsChange(&s, s)));
}

void test_new_sqmeter_url_restarts_http_and_window(void)
{
    const SourceSettings before = base();
    SourceSettings after = before;
    after.httpUrl = "http://other/api/safety";
    const SettingsChange c = settingsChange(&before, after);
    TEST_ASSERT_TRUE(c.sqmeterHttp);
    TEST_ASSERT_TRUE(c.window);
    TEST_ASSERT_FALSE(c.sqmeterMqtt);
    TEST_ASSERT_FALSE(c.nina);
}

void test_new_topic_restarts_mqtt(void)
{
    const SourceSettings before = base();
    SourceSettings after = before;
    after.mqttTopic = "obs/sqmeter";
    const SettingsChange c = settingsChange(&before, after);
    TEST_ASSERT_TRUE(c.sqmeterMqtt);
    TEST_ASSERT_FALSE(c.sqmeterHttp);
}

void test_switching_a_source_on_or_off_restarts_it(void)
{
    const SourceSettings before = base();
    SourceSettings after = before;
    after.httpEnabled = false;
    after.mqttEnabled = false;
    const SettingsChange c = settingsChange(&before, after);
    TEST_ASSERT_TRUE(c.sqmeterHttp && c.sqmeterMqtt && c.window);
}

void test_stale_time_or_contact_restart_the_window_only(void)
{
    const SourceSettings before = base();
    SourceSettings after = before;
    after.staleSeconds = 60;
    SettingsChange c = settingsChange(&before, after);
    TEST_ASSERT_TRUE(c.window);
    TEST_ASSERT_FALSE(c.sqmeterHttp || c.sqmeterMqtt || c.nina);
    after = before;
    after.rainContact = true;
    c = settingsChange(&before, after);
    TEST_ASSERT_TRUE(c.window);
}

void test_new_nina_url_restarts_nina_only(void)
{
    const SourceSettings before = base();
    SourceSettings after = before;
    after.ninaUrl = "http://other:1888";
    const SettingsChange c = settingsChange(&before, after);
    TEST_ASSERT_TRUE(c.nina);
    TEST_ASSERT_FALSE(c.sqmeterHttp || c.sqmeterMqtt || c.window);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_call_restarts_everything);
    RUN_TEST(test_unrelated_save_restarts_nothing);
    RUN_TEST(test_new_sqmeter_url_restarts_http_and_window);
    RUN_TEST(test_new_topic_restarts_mqtt);
    RUN_TEST(test_switching_a_source_on_or_off_restarts_it);
    RUN_TEST(test_stale_time_or_contact_restart_the_window_only);
    RUN_TEST(test_new_nina_url_restarts_nina_only);
    return UNITY_END();
}
