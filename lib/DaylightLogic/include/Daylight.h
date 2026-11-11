#pragma once

// The daylight failsafe's state (spec 013): from the site and the clock, is
// the sun up? With the mode on "alert and close" and the sun up, the device
// wants the roof closed exactly as for unsafe weather (spec 005's sequence),
// and refuses opening. Off and alert never move or refuse anything. Without
// the site or the clock the state is unknown and daylight does nothing. Pure:
// the time comes in as a parameter (constitution V).

#include <cstdint>
#include <string>

#include "Config.h"

namespace SQM
{
    namespace Daylight
    {
        using Mode = DaylightConfig::Mode;

        enum class State : uint8_t
        {
            Off,     // mode off
            Unknown, // mode on, the site or the clock not known
            Night,
            Day,
        };

        // Why the state is unknown.
        enum class Missing : uint8_t
        {
            None,
            Location,
            Clock,
        };

        // Once day, night again only this far below the threshold, so a sun
        // that lingers at the threshold can't flicker the state (research R2).
        constexpr double BAND_DEG = 0.5;
        // The demo's "Sun up": the sun this far above the threshold.
        constexpr double FORCED_ABOVE_DEG = 10.0;

        struct Settings
        {
            Mode mode = Mode::Alert;
            float limitDeg = 0.0f; // the sun's centre, degrees above the horizon
            bool locationSet = false;
            double latitude = 0.0;
            double longitude = 0.0;
        };

        struct View
        {
            Mode mode = Mode::Alert;
            State state = State::Unknown;
            Missing missing = Missing::Location;
            bool known = false;       // altitudeDeg holds a value (night or day)
            double altitudeDeg = 0.0; // meaningful only when known
            float limitDeg = 0.0f;
            // Daylight wants the roof closed now: mode "alert and close" and day.
            bool closes() const { return mode == Mode::Close && state == State::Day; }
            // The mode is on (alert or alert and close).
            bool on() const { return mode != Mode::Off; }
        };

        // The settings as the daylight state uses them.
        Settings settingsFrom(const Config &config);

        // Keeps the band between passes. One instance, updated about once a second.
        class Watch
        {
        public:
            // `clockSet`: the clock is set (Core::CLOCK_VALID_EPOCH); `forceSunUp`:
            // the demo's "Sun up", the sun FORCED_ABOVE_DEG above the threshold
            // whatever the site and the clock.
            View update(const Settings &settings, bool clockSet, int64_t epochSeconds, bool forceSunUp = false);
            const View &view() const { return last; }

        private:
            bool day = false;
            View last;
        };

        // The state's text (constitution III: the value and the limit, or the
        // input that failed): "Sun 3.2° ≥ 0.0°", "Sun -4.1° < 0.0°",
        // "Location not set", "Clock not set", "Off".
        std::string describe(const View &view);
        // What happens to an open roof in daylight: being closed, or nothing
        // because the mode only alerts.
        std::string actionText(Mode mode, bool closing);

        const char *modeName(Mode mode);    // "off", "alert", "close"
        const char *stateName(State state); // "off", "unknown", "night", "day"
        bool parseMode(const std::string &name, Mode &out);
    } // namespace Daylight
} // namespace SQM
