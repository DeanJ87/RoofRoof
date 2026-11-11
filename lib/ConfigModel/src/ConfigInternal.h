#pragma once

// Helpers shared by the ConfigModel sources: defaults (ConfigModel.cpp),
// JSON (ConfigJsonOut.cpp, ConfigJsonIn.cpp) and validation (ConfigValidate.cpp).

#include "Config.h"
#include <ArduinoJson.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace SQM::ConfigDetail
{
    inline constexpr const char *SECRET_MASK = "********";

    constexpr size_t EVENT_COUNT = 10;
    // Events from here on are the imaging-app events, persisted under their own
    // key; the roof's (spec 007) come first, in the main part.
    constexpr size_t FIRST_CLIENT_EVENT = 7;
    // The roof's critical events (spec 007): the first two.
    constexpr size_t CRITICAL_EVENT_COUNT = 2;

    inline bool setError(std::string *error, const std::string &message)
    {
        if (error)
            *error = message;
        return false;
    }

    inline bool inRange(float value, float min, float max)
    {
        return std::isfinite(value) && value >= min && value <= max;
    }

    // JSON key for each configurable event, matching the alert event names.
    template <typename Alerts, typename Setting> std::array<std::pair<const char *, Setting *>, EVENT_COUNT> eventSettingsOf(Alerts &a)
    {
        return {{
            {"mount_not_parked", &a.mountNotParked},
            {"roof_not_closed", &a.roofNotClosed},
            {"move_fault", &a.moveFault},
            {"supply_low", &a.supplyLow},
            {"unsafe_closing", &a.unsafeClosing},
            {"roof_closed", &a.roofClosed},
            {"safety_lost", &a.safetyLost},
            {"client_lost", &a.clientLost},
            {"client_back", &a.clientBack},
            {"client_disconnected", &a.clientDisconnected},
        }};
    }
    inline std::array<std::pair<const char *, const AlertsConfig::EventSetting *>, EVENT_COUNT> eventSettings(const AlertsConfig &a)
    {
        return eventSettingsOf<const AlertsConfig, const AlertsConfig::EventSetting>(a);
    }
    inline std::array<std::pair<const char *, AlertsConfig::EventSetting *>, EVENT_COUNT> eventSettings(AlertsConfig &a)
    {
        return eventSettingsOf<AlertsConfig, AlertsConfig::EventSetting>(a);
    }

    inline const char *sendModeName(AlertsConfig::SendMode mode)
    {
        return mode == AlertsConfig::SendMode::WhileConnected ? "whileConnected" : "any";
    }

    inline const char *switchesName(ParkConfig::Switches switches)
    {
        switch (switches)
        {
        case ParkConfig::Switches::One:
            return "one";
        case ParkConfig::Switches::Two:
            return "two";
        case ParkConfig::Switches::None:
            break;
        }
        return "none";
    }

    // MQTT broker and webhook URL forms, IPv6 literals included (SQMeter spec 015);
    // web/src/validation/configSchema.ts applies the same rules.
    bool validateAddresses(const Config &cfg, std::string *error);

    // What a setting must be; the messages are in ConfigJsonIn.cpp.
    enum class Expected
    {
        String,
        Boolean,
        Number,
        WholeNumber,
        Object,
    };

    template <typename T> constexpr Expected expectedType()
    {
        if constexpr (std::is_same_v<T, std::string>)
            return Expected::String;
        else if constexpr (std::is_same_v<T, bool>)
            return Expected::Boolean;
        else if constexpr (std::is_floating_point_v<T>)
            return Expected::Number;
        else
            return Expected::WholeNumber;
    }

    // Whether a JSON value can be stored in a setting of type T without
    // being changed (ArduinoJson's `value | fallback` uses the fallback
    // whenever this is false).
    template <typename T> bool fits(JsonVariantConst value)
    {
        if constexpr (std::is_same_v<T, std::string>)
            return value.is<const char *>();
        else
            return value.is<T>();
    }

    // Reads settings out of the JSON. A key that's missing keeps the current
    // value. A value of the wrong type (null included) never becomes a
    // default: settings from the UI/API (`strict`) are rejected, naming the
    // field ("Settings: wifi.ssid must be a string"); settings stored by older firmware
    // keep the current value, so one odd stored value can't stop the device
    // loading its settings and dropping off the network.
    class JsonReader
    {
    public:
        explicit JsonReader(bool strict)
            : strictMode(strict)
        {
        }

        bool strict() const { return strictMode; }
        bool failed() const { return !errorField.empty(); }
        // The first wrong-typed field ("wifi.ssid") and what it must be.
        const std::string &field() const { return errorField; }
        Expected expected() const { return errorExpected; }

        // Records the first wrong-typed field (strict only).
        void typeError(const std::string &field, Expected expected)
        {
            if (strictMode && errorField.empty())
            {
                errorField = field;
                errorExpected = expected;
            }
        }

    private:
        bool strictMode;
        std::string errorField;
        Expected errorExpected = Expected::String;
    };

    // One object in the settings JSON ("wifi", "alerts.events.unsafe", ...).
    class Section
    {
    public:
        Section(JsonReader &reader, JsonVariant value, std::string path)
            : readerRef(&reader),
              sectionPath(std::move(path))
        {
            if (value.is<JsonObject>())
                object = value.as<JsonObject>();
            else if (!value.isUnbound() && !sectionPath.empty())
                reader.typeError(sectionPath, Expected::Object);
        }

        Section child(const char *key) const
        {
            if (object.isNull() || !object.containsKey(key))
                return Section(*readerRef, JsonVariant(), field(key));
            return Section(*readerRef, object[key], field(key));
        }

        bool isNull() const { return object.isNull(); }
        bool has(const char *key) const { return !object.isNull() && object.containsKey(key); }
        JsonVariant operator[](const char *key) const { return object[key]; }
        JsonReader &reader() const { return *readerRef; }
        std::string field(const char *key) const { return sectionPath.empty() ? std::string(key) : sectionPath + "." + key; }

        // Whether obj[key] is present and fits T; reports the type error if not.
        template <typename T> bool check(const char *key) const
        {
            if (!has(key))
                return false;
            if (fits<T>(object[key]))
                return true;
            readerRef->typeError(field(key), expectedType<T>());
            return false;
        }

        // Sets `target` from obj[key] when the key is present with the right type.
        template <typename T> void take(const char *key, T &target) const
        {
            if (!check<T>(key))
                return;
            if constexpr (std::is_same_v<T, std::string>)
                target = object[key].as<const char *>();
            else
                target = object[key].as<T>();
        }

    private:
        JsonReader *readerRef;
        JsonObject object;
        std::string sectionPath;
    };

} // namespace SQM::ConfigDetail
