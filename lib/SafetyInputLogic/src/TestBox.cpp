#include "TestBox.h"

#include <ctime>

namespace SQM
{
    namespace TestBox
    {
        namespace
        {
            // Every reason that comes from N.I.N.A. (the switches' stay).
            constexpr uint32_t NINA_REASONS = Safety::ParkNinaNotConfigured | Safety::ParkNinaWaiting | Safety::ParkNinaUnreachable |
                                              Safety::ParkNinaUnreadable | Safety::ParkNinaApiError | Safety::ParkNinaStale |
                                              Safety::ParkMountNotConnected | Safety::ParkMountNotAtPark | Safety::ParkMountSlewing |
                                              Safety::ParkSourcesDisagree;

            void append(std::string &text, bool on, const char *part)
            {
                if (!on)
                    return;
                text += text.empty() ? "" : ", ";
                text += part;
            }
        } // namespace

        Sources sourcesOf(const SafetyConfig &safety, const ParkConfig &park)
        {
            return Sources{!park.ninaUrl.empty(), safety.httpEnabled || safety.mqttEnabled, park.requestPark, park.switchesOnly()};
        }

        uint8_t State::bits() const
        {
            return static_cast<uint8_t>((park ? BitPark : 0) | (weather ? BitWeather : 0) | (daylight ? BitDaylight : 0));
        }

        State evaluate(const TestBoxConfig &config, const Sources &sources, bool clockSet, int64_t nowEpoch)
        {
            State s;
            s.set = config.any();
            s.until = config.until;
            s.parkRequests = sources.ninaConfigured && sources.requestPark;
            if (!s.set)
                return s;
            if (!clockSet)
            {
                s.waitingForClock = true;
                return s;
            }
            if (nowEpoch >= static_cast<int64_t>(config.until))
            {
                s.expired = true;
                return s;
            }
            s.park = config.park && !sources.ninaConfigured && !sources.switchesOnly;
            s.weather = config.weather && !sources.sqmeterOn;
            s.daylight = config.daylight;
            return s;
        }

        bool quietsCriticals(const State &state)
        {
            return state.active() || state.waitingForClock;
        }

        void applyWeather(Safety::WeatherVerdict &verdict, const State &state)
        {
            if (!state.weather || verdict.reasons != Safety::ReasonNoWeatherSource)
                return;
            verdict.verdict = Safety::Verdict::Safe;
            verdict.reasons = 0;
            verdict.bypassed = true;
        }

        void applyPark(Safety::ParkResult &result, Safety::CloseHold &hold, const State &state)
        {
            // Only ever in place of a N.I.N.A. that isn't there: a reading from a
            // real mount is never set aside.
            if (!state.park || (result.reasons & Safety::ParkNinaNotConfigured) == 0)
                return;
            result.reasons &= ~NINA_REASONS;
            result.disagree = false;
            if (result.reasons != 0)
                return;
            result.parked = true;
            result.bypassed = true;
            hold = Safety::CloseHold{};
            hold.parked = true;
        }

        std::string formatUtc(uint32_t epoch)
        {
            const time_t t = static_cast<time_t>(epoch);
            struct tm utc;
            gmtime_r(&t, &utc);
            char text[32];
            std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M UTC", &utc);
            return text;
        }

        std::string describe(const State &s)
        {
            if (!s.set)
                return "";
            if (s.waitingForClock)
                return "Test box bypasses wait for the clock: the checks apply until it is set";
            if (s.expired && s.until == 0)
                return "Test box bypasses set but not started (restored settings): save or Renew to start them";
            if (s.expired)
                return "Test box bypasses expired at " + formatUtc(s.until) + ": the checks are back";
            std::string parts;
            append(parts, s.park, "park check (no telescope)");
            append(parts, s.weather, "weather check (no weather source)");
            append(parts, s.daylight, "daylight close");
            if (parts.empty())
                return "Test box bypasses set but not in effect: a N.I.N.A. address or weather source is set";
            return "Test box: " + parts + " bypassed until " + formatUtc(s.until);
        }

        std::string domePrefix(const State &s)
        {
            if (!s.active())
                return "";
            std::string parts;
            append(parts, s.park, "park check");
            append(parts, s.weather, "weather check");
            append(parts, s.daylight, "daylight close");
            return "TEST BOX (bypassed: " + parts + "): ";
        }
    } // namespace TestBox
} // namespace SQM
