#include "Config.h"
#include "ConfigInternal.h"
#include "ConfigPins.h"

#include <utility>

// Settings groups added late, as JSON in (applyJson in ConfigJsonIn.cpp calls
// these): spec 023's pins, analog front end and network mode, and spec 022's test box.

namespace SQM::ConfigDetail
{
    // Spec 023. An unknown mode is refused, never read as a default.
    bool applyNetwork(const Section &network, NetworkConfig &n, std::string *error)
    {
        if (!network.has("mode"))
            return true;
        const std::string mode = network["mode"].is<const char *>() ? network["mode"].as<const char *>() : "";
        const std::pair<const char *, NetworkConfig::Mode> modes[] = {
            {"auto", NetworkConfig::Mode::Auto},
            {"ethernet", NetworkConfig::Mode::Ethernet},
            {"wifi", NetworkConfig::Mode::Wifi},
        };
        for (const auto &choice : modes)
            if (mode == choice.first)
            {
                n.mode = choice.second;
                return true;
            }
        // Stored by a later release with a mode this one doesn't know: Auto, never a lost store.
        if (!network.reader().strict())
        {
            n.mode = NetworkConfig::Mode::Auto;
            return true;
        }
        return setError(error, "Network: mode must be auto, ethernet or wifi");
    }

    // Spec 023 (ConfigPins::read): an unknown name is refused, stored or
    // not, so settings whose pins can't be read hold every motor pin at boot.
    bool applyPins(const Section &root, Board::PinMap &map, std::string *error)
    {
        // Stored by a release before spec 023: that device runs the DevKit
        // map, so it keeps it (a new device's settings always have pins).
        if (!root.has("pins") && !root.reader().strict())
            map = Board::presetMap(Board::Preset::Devkit);
        if (!root.has("pins"))
            return true;
        JsonVariant pins = root["pins"];
        if (!pins.is<JsonObject>())
            return setError(error, "Settings: pins must be an object");
        return ConfigPins::read(pins.as<JsonObjectConst>(), map, error);
    }

    bool applyAnalog(const Section &root, Board::AnalogConfig &analog, std::string *error)
    {
        if (!root.has("analog"))
            return true;
        JsonVariant value = root["analog"];
        if (!value.is<JsonObject>())
            return setError(error, "Settings: analog must be an object");
        return ConfigPins::readAnalog(value.as<JsonObjectConst>(), analog, error);
    }

    // Spec 022. The deadline is the device's: read from stored settings
    // only, never from a request or a restore (strict).
    void applyTestBox(const Section &t, TestBoxConfig &c)
    {
        t.take("park", c.park);
        t.take("weather", c.weather);
        t.take("daylight", c.daylight);
        t.take("hours", c.hours);
        if (!t.reader().strict())
            t.take("until", c.until);
    }
} // namespace SQM::ConfigDetail
