#include "Config.h"
#include "ConfigInternal.h"
#include <ArduinoJson.h>
#include <cstring>

// Spec 024: the park source and the fitted switches, in (Config::applyJson),
// and the combinations a change from the UI/API may make.

namespace SQM::ConfigDetail
{
    namespace
    {
        bool applyParkSwitches(const Section &park, ParkConfig &p, std::string *error)
        {
            const std::string switches = park["switches"].is<const char *>() ? park["switches"].as<const char *>() : "";
            for (ParkConfig::Switches choice : {ParkConfig::Switches::None, ParkConfig::Switches::One, ParkConfig::Switches::Two})
            {
                if (switches == switchesName(choice))
                {
                    p.switches = choice;
                    return true;
                }
            }
            return setError(error, "Park switches must be none, one or two");
        }

        // Spec 024: the source sent, or, with the switches sent alone (settings
        // saved before 024, an older page, a script), the rule they meant then.
        bool applyParkSource(const Section &park, ParkConfig &p, std::string *error)
        {
            if (!park.has("source"))
            {
                if (park.has("switches"))
                    p.source = p.switches == ParkConfig::Switches::None ? ParkConfig::Source::Nina : ParkConfig::Source::NinaAndSwitches;
                return true;
            }
            const std::string source = park["source"].is<const char *>() ? park["source"].as<const char *>() : "";
            for (ParkConfig::Source choice : {ParkConfig::Source::Nina, ParkConfig::Source::Switches, ParkConfig::Source::NinaAndSwitches})
            {
                if (source == parkSourceName(choice))
                {
                    p.source = choice;
                    return true;
                }
            }
            return setError(error, "Park source must be nina, switches or ninaAndSwitches");
        }
    } // namespace

    // Named choices: anything but the documented names is refused, never
    // read as a default (a typo must not change what confirms the park).
    bool applyPark(const Section &park, ParkConfig &p, std::string *error)
    {
        const Section nina = park.child("nina");
        nina.take("url", p.ninaUrl);
        park.take("timeoutSeconds", p.timeoutSeconds);
        park.take("requestPark", p.requestPark);
        if (nina.has("api"))
        {
            const char *api = nina["api"].is<const char *>() ? nina["api"].as<const char *>() : "";
            if (std::strcmp(api, "v2") != 0)
                return setError(error, "N.I.N.A. API version must be v2");
            p.ninaApi = ParkConfig::NinaApi::V2;
        }
        if (park.has("switches") && !applyParkSwitches(park, p, error))
            return false;
        return applyParkSource(park, p, error);
    }

    // Spec 024: the source and the fitted switches agree, and the test box's
    // park bypass (it stands in for N.I.N.A.) only where N.I.N.A. counts.
    bool validateParkSource(const Config &cfg, std::string *error)
    {
        const bool fitted = cfg.park.switches != ParkConfig::Switches::None;
        if (cfg.park.source == ParkConfig::Source::Nina && fitted)
            return setError(error, "Park: N.I.N.A. only uses no park switch; choose N.I.N.A. and park switches");
        if (cfg.park.source != ParkConfig::Source::Nina && !fitted)
            return setError(error, "Park: park switches need one or two fitted; choose N.I.N.A. only for none");
        if (cfg.testBox.park && cfg.park.switchesOnly())
            return setError(error, "Test box: the park bypass stands in for N.I.N.A.; with park switches only there is nothing to bypass");
        return true;
    }
} // namespace SQM::ConfigDetail
