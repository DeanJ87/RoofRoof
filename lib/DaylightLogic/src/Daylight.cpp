#include "Daylight.h"

#include <cstdio>

#include "SunPosition.h"

namespace SQM
{
    namespace Daylight
    {
        namespace
        {
            std::string degrees(double value)
            {
                char text[24];
                std::snprintf(text, sizeof(text), "%.1f°", value);
                return text;
            }
        } // namespace

        Settings settingsFrom(const Config &config)
        {
            Settings s;
            s.mode = config.daylight.mode;
            s.limitDeg = config.daylight.sunAltitudeDeg;
            s.locationSet = config.location.set;
            s.latitude = config.location.latitude;
            s.longitude = config.location.longitude;
            s.bandDeg = config.daylight.bandDeg;
            return s;
        }

        View Watch::update(const Settings &settings, bool clockSet, int64_t epochSeconds, bool forceSunUp)
        {
            // A mode or threshold change decides from scratch (FR-004).
            if (settings.mode != lastMode || settings.limitDeg != lastLimitDeg)
                day = false;
            lastMode = settings.mode;
            lastLimitDeg = settings.limitDeg;
            View v;
            v.mode = settings.mode;
            v.limitDeg = settings.limitDeg;
            // Spec 014: the band is a setting, clamped to its range (never 0: the state would flicker).
            v.bandDeg = !(settings.bandDeg >= MIN_BAND_DEG) ? MIN_BAND_DEG
                        : settings.bandDeg > MAX_BAND_DEG   ? MAX_BAND_DEG
                                                            : settings.bandDeg; // NaN too
            v.missing = Missing::None;
            if (settings.mode == Mode::Off)
            {
                v.state = State::Off;
            }
            else if (forceSunUp)
            {
                v.known = true;
                v.altitudeDeg = static_cast<double>(settings.limitDeg) + FORCED_ABOVE_DEG;
            }
            else if (!settings.locationSet)
            {
                v.state = State::Unknown;
                v.missing = Missing::Location;
            }
            else if (!clockSet)
            {
                v.state = State::Unknown;
                v.missing = Missing::Clock;
            }
            else
            {
                v.known = true;
                v.altitudeDeg = Astro::sunElevationDeg(epochSeconds, settings.latitude, settings.longitude);
            }
            if (v.known)
            {
                // Day at or above the threshold; night again only below the band.
                const double limit = static_cast<double>(settings.limitDeg);
                day = v.altitudeDeg >= limit || (day && v.altitudeDeg >= limit - static_cast<double>(v.bandDeg));
                v.state = day ? State::Day : State::Night;
            }
            else
            {
                day = false; // the next known altitude decides from scratch
            }
            last = v;
            return v;
        }

        std::string describe(const View &view)
        {
            switch (view.state)
            {
            case State::Off:
                return "Off";
            case State::Unknown:
                return view.missing == Missing::Clock ? "Clock not set" : "Location not set";
            case State::Day:
                if (view.altitudeDeg < static_cast<double>(view.limitDeg))
                    return "Sun " + degrees(view.altitudeDeg) + ", day until < " +
                           degrees(static_cast<double>(view.limitDeg) - static_cast<double>(view.bandDeg));
                return "Sun " + degrees(view.altitudeDeg) + " ≥ " + degrees(view.limitDeg);
            case State::Night:
                break;
            }
            return "Sun " + degrees(view.altitudeDeg) + " < " + degrees(view.limitDeg);
        }

        std::string actionText(Mode mode, bool closing)
        {
            if (closing)
                return "The mount is being parked and the roof closed.";
            if (mode == Mode::Close)
                return "The automatic close can't close it now: see the roof's other alerts.";
            return "Daylight is set to alert only, so nothing closes it.";
        }

        const char *modeName(Mode mode)
        {
            switch (mode)
            {
            case Mode::Off:
                return "off";
            case Mode::Close:
                return "close";
            case Mode::Alert:
                break;
            }
            return "alert";
        }

        const char *stateName(State state)
        {
            static constexpr const char *NAMES[] = {"off", "unknown", "night", "day"};
            const size_t index = static_cast<size_t>(state);
            return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "unknown";
        }

        bool parseMode(const std::string &name, Mode &out)
        {
            if (name == "off")
                out = Mode::Off;
            else if (name == "alert")
                out = Mode::Alert;
            else if (name == "close")
                out = Mode::Close;
            else
                return false;
            return true;
        }
    } // namespace Daylight
} // namespace SQM
