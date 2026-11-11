#include "SafetyInputs.h"

#include "Logger.h"
#include "MQTTClient.h"
#include "SqmeterReply.h"
#include <Arduino.h>

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Safety";

        SafetyPoller::Targets targetsFor(const Config &config)
        {
            SafetyPoller::Targets targets;
            targets.sqmeterEnabled = config.safety.httpEnabled;
            targets.sqmeterUrl = config.safety.httpUrl;
            targets.ninaUrl = config.park.ninaUrl;
            targets.ninaApi = Nina::ApiVersion::V2;
            return targets;
        }

        // What changes the log line: not the ages, which tick every second.
        std::string weatherKey(const Safety::WeatherVerdict &v)
        {
            return std::to_string(static_cast<int>(v.verdict)) + "/" + std::to_string(v.reasons) + "/" +
                   std::to_string(static_cast<int>(v.http.state)) + "/" + std::to_string(static_cast<int>(v.mqtt.state)) + "/" +
                   std::to_string(v.http.sqmeterFlags) + "/" + std::to_string(v.mqtt.sqmeterFlags);
        }

        std::string parkKey(const Safety::ParkResult &r)
        {
            return std::to_string(r.parked) + "/" + std::to_string(r.reasons);
        }

        Safety::SwitchReading switchReading(bool closed)
        {
            return closed ? Safety::SwitchReading::Closed : Safety::SwitchReading::Open;
        }
    } // namespace

    SafetyInputs::SafetyInputs(SafetyPoller &poller, MQTTClient *mqtt)
        : poller(poller),
          mqtt(mqtt)
    {
    }

    void SafetyInputs::begin(const Config &config, uint32_t nowMs)
    {
        applyConfig(config, nowMs);
        poller.begin(targetsFor(config));
    }

    void SafetyInputs::updateConfig(const Config &config, uint32_t nowMs)
    {
        applyConfig(config, nowMs);
        poller.configure(targetsFor(config));
    }

    // New settings start every source again: a source just switched on gets
    // its own start-up window (unknown, never safe) before it counts as lost.
    void SafetyInputs::applyConfig(const Config &config, uint32_t nowMs)
    {
        safety = config.safety;
        parkConfig = config.park;
        startedAtMs = nowMs;
        mqttRecord = Safety::SourceRecord{};
        // dep: D-40 - the topics are read from RoofRoof's own broker, so with
        // MQTT off they never arrive and the source goes stale (unsafe).
        mqttRecord.enabled = safety.mqttEnabled;
        mqttBase = safety.mqttTopic;
        evaluated = false;
        if (mqtt == nullptr)
            return;
        std::vector<std::string> topics;
        if (safety.mqttEnabled)
            topics = {mqttBase + "/availability", mqttBase + "/safe", mqttBase + "/safety"};
        mqtt->watch(topics, [this](const std::string &topic, const std::string &payload) { onMqttMessage(topic, payload); });
    }

    void SafetyInputs::onMqttMessage(const std::string &topic, const std::string &payload)
    {
        const uint32_t now = millis();
        if (topic == mqttBase + "/availability")
        {
            const auto online = Safety::parseAvailability(payload);
            if (!online)
                mqttRecord.recordFailure(Safety::Failure::Unreadable, "availability payload not online/offline", now);
            else
                mqttRecord.availability = *online ? Safety::Availability::Online : Safety::Availability::Offline;
            return;
        }
        if (topic == mqttBase + "/safe")
        {
            const auto safe = Safety::parseSafePayload(payload);
            if (!safe)
            {
                mqttRecord.recordFailure(Safety::Failure::Unreadable, "safe payload not 1/0", now);
                return;
            }
            // SQMeter publishes <base>/safety just before <base>/safe, so its
            // flags are the latest; a bare "1" never clears them.
            Safety::SqmeterReply reply = mqttRecord.last;
            reply.safe = *safe;
            reply.evaluatedAgeMs = 0;
            mqttRecord.recordGood(reply, now);
            return;
        }
        if (topic != mqttBase + "/safety")
            return;
        Safety::SqmeterReply reply;
        const Safety::ReplyError error = Safety::parseSafetyJson(payload, reply);
        if (error == Safety::ReplyError::None)
            mqttRecord.recordGood(reply, now);
        else
            mqttRecord.recordFailure(Safety::Failure::Unreadable, std::string("safety ") + Safety::replyErrorText(error), now);
    }

    void SafetyInputs::setRainContact(bool raining)
    {
        rainReading = raining ? Safety::RainContact::Raining : Safety::RainContact::Dry;
    }

    void SafetyInputs::setParkSwitches(bool switch1Closed, bool switch2Closed)
    {
        switch1 = switchReading(switch1Closed);
        switch2 = switchReading(switch2Closed);
    }

    void SafetyInputs::handle(uint32_t nowMs)
    {
        if (evaluated && nowMs - lastEvaluateMs < EVALUATE_EVERY_MS)
            return;
        lastEvaluateMs = nowMs;
        evaluated = true;
        evaluate(nowMs);
        logChanges();
    }

    void SafetyInputs::evaluate(uint32_t nowMs)
    {
        const SafetyPoller::Snapshot snap = poller.snapshot();

        Safety::WeatherInputs in;
        in.http = snap.sqmeter;
        in.mqtt = mqttRecord;
        in.rainContact = safety.rainContact ? rainReading : Safety::RainContact::NotFitted;
        in.httpLimitMs = safety.staleSeconds * 1000;
        in.startedAtMs = startedAtMs;
        in.nowMs = nowMs;
        verdict = Safety::evaluateWeather(in);

        Safety::ParkInputs park;
        park.nina = snap.nina;
        park.fittedSwitches = static_cast<uint8_t>(parkConfig.switches);
        park.switch1 = switch1;
        park.switch2 = switch2;
        park.nowMs = nowMs;
        parkResult = Safety::evaluatePark(park);
    }

    void SafetyInputs::logChanges()
    {
        const std::string weather = weatherKey(verdict);
        if (weather != loggedWeatherKey)
        {
            loggedWeatherKey = weather;
            Logger::info(TAG, "Weather %s", Safety::describe(verdict).c_str());
        }
        const std::string park = parkKey(parkResult);
        if (park != loggedParkKey)
        {
            loggedParkKey = park;
            Logger::info(TAG, "Park: %s", Safety::describe(parkResult).c_str());
        }
    }
} // namespace SQM
