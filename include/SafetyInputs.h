#pragma once

#include "Config.h"
#include "ParkRule.h"
#include "SafetyInput.h"
#include "SafetyPoller.h"
#include <cstdint>
#include <string>
#include <vector>

namespace SQM
{
    class MQTTClient;

    // The weather verdict and the park result (spec 004), kept up to date once
    // a second on the main loop from the poller's records, SQMeter's MQTT
    // topics and the GPIO readings spec 003 supplies. The decisions are
    // lib/SafetyInputLogic's; this only gathers the inputs and logs changes.
    // Spec 005 acts on weather() and park(); nothing here moves the roof.
    class SafetyInputs
    {
    public:
        SafetyInputs(SafetyPoller &poller, MQTTClient *mqtt);

        // Main loop only, as are all the members below.
        void begin(const Config &config, uint32_t nowMs);
        void updateConfig(const Config &config, uint32_t nowMs);
        void handle(uint32_t nowMs);

        // The GPIO inputs (spec 003). Until they are supplied, a fitted rain
        // contact or park switch reads "unreadable": unsafe and not parked.
        void setRainContact(bool raining);
        void setParkSwitches(bool switch1Closed, bool switch2Closed);

        const Safety::WeatherVerdict &weather() const { return verdict; }
        const Safety::ParkResult &park() const { return parkResult; }
        // Ask N.I.N.A. to park (spec 005). The answer is in the poller's
        // snapshot; only park() says the mount is parked.
        void requestPark() { poller.requestPark(); }

    private:
        static constexpr uint32_t EVALUATE_EVERY_MS = 1000;

        void applyConfig(const Config &config, uint32_t nowMs);
        void onMqttMessage(const std::string &topic, const std::string &payload);
        void evaluate(uint32_t nowMs);
        void logChanges();

        SafetyPoller &poller;
        MQTTClient *mqtt;
        SafetyConfig safety;
        ParkConfig parkConfig;
        std::string mqttBase; // SQMeter's base topic, e.g. "sqmeter"
        Safety::SourceRecord mqttRecord;
        Safety::RainContact rainReading = Safety::RainContact::Unreadable;
        Safety::SwitchReading switch1 = Safety::SwitchReading::Unreadable;
        Safety::SwitchReading switch2 = Safety::SwitchReading::Unreadable;
        uint32_t startedAtMs = 0;
        uint32_t lastEvaluateMs = 0;
        bool evaluated = false;

        Safety::WeatherVerdict verdict;
        Safety::ParkResult parkResult;
        // What was last logged: the verdict, reasons and source states.
        std::string loggedWeatherKey;
        std::string loggedParkKey;
    };
} // namespace SQM
