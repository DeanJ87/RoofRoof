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

    constexpr size_t EVENT_COUNT = 16;
    // Events from here on are the imaging-app events and the daylight events
    // (spec 013), persisted under their own key; the roof's (spec 007) come
    // first, in the main part.
    constexpr size_t FIRST_CLIENT_EVENT = 8;
    // The roof's critical events (spec 007, spec 030): the first three.
    constexpr size_t CRITICAL_EVENT_COUNT = 3;

    inline bool setError(std::string *error, const std::string &message)
    {
        if (error)
            *error = message;
        return false;
    }

    // The location's ranges (ConfigValidate.cpp), also checked on a save the
    // device's own location overrides (spec 020).
    bool validateLocation(const LocationConfig &location, std::string *error);

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
            {"needs_person", &a.needsPerson},
            {"move_fault", &a.moveFault},
            {"supply_low", &a.supplyLow},
            {"unsafe_closing", &a.unsafeClosing},
            {"roof_closed", &a.roofClosed},
            {"safety_lost", &a.safetyLost},
            {"client_lost", &a.clientLost},
            {"client_back", &a.clientBack},
            {"client_disconnected", &a.clientDisconnected},
            {"daylight_open", &a.daylightOpen},
            {"daylight_closed", &a.daylightClosed},
            {"daylight_unknown", &a.daylightUnknown},
            {"test_box", &a.testBox},
            {"nina_lost", &a.ninaLost},
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

    inline const char *daylightModeName(DaylightConfig::Mode mode)
    {
        switch (mode)
        {
        case DaylightConfig::Mode::Off:
            return "off";
        case DaylightConfig::Mode::Close:
            return "close";
        case DaylightConfig::Mode::Alert:
            break;
        }
        return "alert";
    }

    // Spec 044: autoClose.personStopLapse's names.
    inline const char *personStopLapseName(AutoCloseConfig::PersonStopLapse lapse)
    {
        switch (lapse)
        {
        case AutoCloseConfig::PersonStopLapse::Rain:
            return "rain";
        case AutoCloseConfig::PersonStopLapse::Always:
            return "always";
        case AutoCloseConfig::PersonStopLapse::Never:
            break;
        }
        return "never";
    }

    inline const char *setupStateName(SetupConfig::State state)
    {
        switch (state)
        {
        case SetupConfig::State::Offered:
            return "offered";
        case SetupConfig::State::Done:
            return "done";
        case SetupConfig::State::New:
            break;
        }
        return "new";
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

    inline const char *parkSourceName(ParkConfig::Source source)
    {
        switch (source)
        {
        case ParkConfig::Source::Switches:
            return "switches";
        case ParkConfig::Source::NinaAndSwitches:
            return "ninaAndSwitches";
        case ParkConfig::Source::Nina:
            break;
        }
        return "nina";
    }

    class Section;

    // Spec 024 (ConfigJsonPark.cpp): the park section in, and whether the
    // source, the fitted switches and the test box's park bypass agree.
    bool applyPark(const Section &park, ParkConfig &p, std::string *error);
    bool validateParkSource(const Config &cfg, std::string *error);

    // Spec 024 (CopiedSecrets.cpp): each secret sent as Config::COPIED_SECRET
    // takes `copied`'s value for the same server, or the save is refused.
    bool fillCopiedSecrets(Config &cfg, const CopiedSecrets *copied, std::string *error);

    // MQTT broker and webhook URL forms, IPv6 literals included (SQMeter spec 015);
    // web/src/validation/configSchema.ts applies the same rules.
    bool validateAddresses(const Config &cfg, std::string *error);
    bool validateSqmeterAddress(const SafetyConfig &s, std::string *error);

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

    // Spec 020 (ConfigJsonSite.cpp): the location section, the follow
    // switches, and their upgrade rule for settings saved before them.
    bool applyLocation(const Section &location, LocationConfig &loc, std::string *error);
    void applySync(const Section &sync, SqmeterSyncConfig &s);
    // Spec 026: each sync group's own values (time, mqtt, alerts), to tell an
    // edit of a following group from a save that leaves it alone.
    struct SyncedValues
    {
        uint32_t group[3]; // a hash of each group's values
    };
    SyncedValues syncedValues(const Config &cfg);
    // Spec 026: the RoofConfig::MEASURED_* bits of the values that differ between `a` and `b`.
    uint8_t typedOver(const RoofConfig &a, const RoofConfig &b);
    // Spec 026: a save from the UI/API that changes a following group's values
    // and doesn't say whether it follows makes that group local, so the next
    // sync never undoes what a person typed.
    void unfollowEdited(const Section &sync, const SyncedValues &before, Config &cfg);
    void migrateSync(Config &cfg);

    // Spec 023 (ConfigJsonBoard.cpp): the pins, the analog front end and the network mode.
    bool applyPins(const Section &root, Board::PinMap &map, std::string *error);
    bool applyAnalog(const Section &root, Board::AnalogConfig &analog, std::string *error);
    bool applyNetwork(const Section &network, NetworkConfig &n, std::string *error);
    // Spec 022.
    void applyTestBox(const Section &t, TestBoxConfig &c);
    // Spec 014's autoClose.notStarted and spec 044's autoClose.personStopLapse (ConfigJsonGroups.cpp).
    bool applyAutoCloseChoices(const Section &autoClose, AutoCloseConfig &c, std::string *error);

} // namespace SQM::ConfigDetail
