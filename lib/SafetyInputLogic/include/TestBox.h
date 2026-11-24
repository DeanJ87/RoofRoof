#pragma once

// Spec 022 (constitution 3.0.0, Principles II and III): the test box's
// bypasses for a box with no telescope under it and no weather station. Set
// only by the owner in the settings (TestBoxConfig), in effect only while the
// clock is set and before their deadline, and never beside the source they
// stand in for: the park bypass replaces N.I.N.A. (fitted park switches still
// count), the weather bypass replaces a missing SQMeter (rain from any source
// still counts; spec 023 removed the local rain contact). No fault, the watchdog or the supply check is touched. Applied
// once, where the verdict and the park result are made, so every check sees
// the same thing. Pure: the clock comes in as parameters (constitution V).

#include <cstdint>
#include <string>

#include "Config.h"
#include "ParkRule.h"
#include "SafetyInput.h"

namespace SQM
{
    namespace TestBox
    {
        enum Bit : uint8_t
        {
            BitPark = 1u << 0,
            BitWeather = 1u << 1,
            BitDaylight = 1u << 2,
        };

        // The sources a bypass may not sit beside.
        struct Sources
        {
            bool ninaConfigured = false; // a N.I.N.A. address is set
            bool sqmeterOn = false;      // SQMeter HTTP or MQTT is on
            bool requestPark = true;     // park.requestPark: the automatic close may ask N.I.N.A. to park
            bool switchesOnly = false;   // spec 024: the park source doesn't use N.I.N.A.
        };
        Sources sourcesOf(const SafetyConfig &safety, const ParkConfig &park);

        struct State
        {
            bool set = false; // any bypass set in the settings
            // Each bypass set and in effect now.
            bool park = false;
            bool weather = false;
            bool daylight = false;
            bool expired = false;         // set, the clock set and the deadline passed (or none)
            bool waitingForClock = false; // set and the clock not set
            uint32_t until = 0;           // the deadline, Unix seconds
            // Unsafe weather would ask a configured N.I.N.A. to park its mount
            // (park.requestPark on and an address set): shown with the bypasses.
            bool parkRequests = false;
            bool active() const { return park || weather || daylight; }
            uint8_t bits() const;
        };

        // `clockSet`: the clock holds a real time (Core::CLOCK_VALID_EPOCH).
        State evaluate(const TestBoxConfig &config, const Sources &sources, bool clockSet, int64_t nowEpoch);

        // The weather bypass: safe when "no weather source" is the only reason.
        void applyWeather(Safety::WeatherVerdict &verdict, const State &state);
        // The park bypass: N.I.N.A. not configured is set aside; parked when no
        // fitted switch says otherwise. The hold a move rides on is then the
        // switches alone (no N.I.N.A. reading, no freshness bound).
        void applyPark(Safety::ParkResult &result, Safety::CloseHold &hold, const State &state);

        // "Test box: park check (no telescope), weather check (no weather
        // source) bypassed until 2026-11-26 12:26 UTC", the expired or
        // waiting text, or "" when nothing is set.
        std::string describe(const State &state);
        // The Dome's description prefix: "TEST BOX (bypassed: park check,
        // weather check): ", or "" when none is in effect.
        std::string domePrefix(const State &state);
        // "2026-11-26 12:26 UTC".
        std::string formatUtc(uint32_t epoch);
    } // namespace TestBox
} // namespace SQM
