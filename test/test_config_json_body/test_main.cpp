// Spec 029 research R13: GET /api/config fills a document the firmware
// allocated itself (every allocation checked) instead of growing a string.
// The document must be the same as Config::toJson, and the longest settings
// (every custom alert text at its limit, long addresses) must fit the pool,
// or the reply would be cut or refused.

#include <unity.h>

#include <ArduinoJson.h>
#include <string>

#include "Config.h"
#include "ConfigJson.h"

using namespace SQM;

void setUp() {}
void tearDown() {}

namespace
{
    std::string body(const Config &c, bool &overflowed)
    {
        DynamicJsonDocument doc(ConfigJson::CAPACITY);
        ConfigJson::write(c, doc, true);
        overflowed = doc.overflowed();
        std::string out(measureJson(doc), '\0');
        serializeJson(doc, &out[0], out.size() + 1);
        return out;
    }

    Config longest()
    {
        Config c = Config::createDefault();
        c.deviceName = std::string(32, 'n');
        c.safety.httpEnabled = true;
        c.safety.httpUrl = "http://" + std::string(120, 'a') + ":8080";
        c.park.ninaUrl = "http://" + std::string(120, 'b') + ":1888";
        c.mqtt.broker = std::string(64, 'm');
        c.mqtt.topic = std::string(64, 't');
        c.alerts.webhookUrl = "https://" + std::string(200, 'w');
        c.alerts.ntfyServer = "https://" + std::string(100, 's');
        c.alerts.ntfyTopic = std::string(64, 'o');
        for (AlertsConfig::EventSetting *e :
             {&c.alerts.mountNotParked,
              &c.alerts.roofNotClosed,
              &c.alerts.moveFault,
              &c.alerts.supplyLow,
              &c.alerts.unsafeClosing,
              &c.alerts.roofClosed,
              &c.alerts.safetyLost,
              &c.alerts.clientLost,
              &c.alerts.clientBack,
              &c.alerts.clientDisconnected,
              &c.alerts.daylightOpen,
              &c.alerts.daylightClosed,
              &c.alerts.daylightUnknown,
              &c.alerts.testBox})
        {
            e->title = std::string(AlertsConfig::MAX_TEMPLATE_TITLE, 'T');
            e->message = std::string(AlertsConfig::MAX_TEMPLATE_MESSAGE, 'M');
            e->sound = "cosmic";
        }
        return c;
    }
} // namespace

void test_body_is_the_settings_json()
{
    bool overflowed = true;
    const Config c = Config::createDefault();
    TEST_ASSERT_EQUAL_STRING(c.toJson(true).c_str(), body(c, overflowed).c_str());
    TEST_ASSERT_FALSE(overflowed);
}

void test_longest_settings_fit_the_pool()
{
    bool overflowed = true;
    const Config c = longest();
    const std::string json = body(c, overflowed);
    TEST_ASSERT_FALSE(overflowed);
    TEST_ASSERT_EQUAL_STRING(c.toJson(true).c_str(), json.c_str());
    // The firmware writes the text into a buffer the size of the pool.
    TEST_ASSERT_TRUE(json.size() + 1 < ConfigJson::CAPACITY);
    const auto again = Config::fromJson(json);
    TEST_ASSERT_TRUE(again.has_value());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_body_is_the_settings_json);
    RUN_TEST(test_longest_settings_fit_the_pool);
    return UNITY_END();
}
