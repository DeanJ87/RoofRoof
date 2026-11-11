#pragma once

#include "Config.h"
#include "ParkRule.h"
#include "SafetyInput.h"
#include "SafetyPoller.h"
#include "SqmeterSource.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <optional>
#include <string>

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
        ~SafetyInputs();
        SafetyInputs(const SafetyInputs &) = delete;
        SafetyInputs &operator=(const SafetyInputs &) = delete;

        // Main loop.
        void begin(const Config &config);
        // Any task (a settings save runs on the web server's task): the new
        // settings are handed over and applied by the next handle().
        void updateConfig(const Config &config);
        // Main loop, every pass.
        void handle();

        // The GPIO inputs (spec 003), main loop. Until they are supplied, a
        // fitted rain contact or park switch reads Unreadable: unsafe and not parked.
        void setRainContact(Safety::RainContact reading) { rainReading = reading; }
        void setParkSwitches(Safety::SwitchReading first, Safety::SwitchReading second);

        // Main loop.
        const Safety::WeatherVerdict &weather() const { return verdict; }
        const Safety::ParkResult &park() const { return parkResult; }
        // Ask N.I.N.A. to park (spec 005). The answer is in the poller's
        // snapshot; only park() says the mount is parked.
        void requestPark() { poller.requestPark(); }

    private:
        static constexpr uint32_t EVALUATE_EVERY_MS = 1000;

        // The settings this class uses, as handed over between tasks.
        struct Settings
        {
            SafetyConfig safety;
            ParkConfig park;
        };

        static Settings settingsOf(const Config &config);
        void apply(const Settings &next, uint64_t nowMs);
        void watchTopics();
        void onMqttMessage(const std::string &topic, const std::string &payload);
        void evaluate(uint64_t nowMs);
        void logChanges();

        SafetyPoller &poller;
        MQTTClient *mqtt;
        SemaphoreHandle_t pendingLock = nullptr;
        std::optional<Settings> pending; // guarded by pendingLock
        Settings settings;
        bool started = false;

        Safety::MqttTopics topics;
        Safety::SourceRecord mqttRecord;
        uint32_t seenConnections = 0; // MQTT (re)connects already handled
        Safety::RainContact rainReading = Safety::RainContact::Unreadable;
        Safety::SwitchReading switch1 = Safety::SwitchReading::Unreadable;
        Safety::SwitchReading switch2 = Safety::SwitchReading::Unreadable;
        uint64_t startedAtMs = 0;
        uint64_t lastEvaluateMs = 0;
        bool evaluated = false;

        Safety::WeatherVerdict verdict;
        Safety::ParkResult parkResult;
        // What was last logged.
        bool logged = false;
        Safety::WeatherVerdict loggedVerdict;
        Safety::ParkResult loggedPark;
    };
} // namespace SQM
