#include "RoofGates.h"

#include <cstdio>
#include <string>

namespace SQM
{
    namespace RoofIo
    {
        GateInputs currentGates(const GateInputs &in, bool handedOver, uint32_t ageMs)
        {
            if (handedOver && ageMs <= SAFETY_HANDOVER_STALE_MS)
                return in;
            GateInputs stale = in;
            char text[96];
            if (handedOver)
                std::snprintf(
                    text,
                    sizeof(text),
                    "safety inputs not updated for %u.%u s > %u s",
                    static_cast<unsigned>(ageMs / 1000),
                    static_cast<unsigned>(ageMs % 1000 / 100),
                    static_cast<unsigned>(SAFETY_HANDOVER_STALE_MS / 1000));
            else
                std::snprintf(text, sizeof(text), "safety inputs not evaluated yet");
            stale.weather = Safety::Verdict::Unknown;
            stale.weatherText = text;
            stale.parked = false;
            stale.parkText = text;
            stale.ninaGrace = false;
            stale.parkUnanswered = false; // spec 047: a stalled handover is not N.I.N.A. not answering
            return stale;
        }

        bool switchesClosed(uint8_t fitted, Contact switch1, Contact switch2)
        {
            if (fitted >= 1 && switch1 != Contact::Closed)
                return false;
            if (fitted >= 2 && switch2 != Contact::Closed)
                return false;
            return true;
        }

        bool parkHolds(const GateInputs &in)
        {
            return in.parked && switchesClosed(in.fittedSwitches, in.switch1, in.switch2);
        }

        bool weatherOverridable(const GateInputs &in)
        {
            return in.weather == Safety::Verdict::Unsafe && in.weatherReasons == Safety::ReasonWeatherUnsafe;
        }

        uint8_t overridableNow(Roof::Command command, const GateInputs &in)
        {
            if (command == Roof::Command::Close)
                return parkHolds(in) ? 0 : OverridePark;
            if (command != Roof::Command::Open)
                return 0;
            return static_cast<uint8_t>(
                (weatherOverridable(in) ? OverrideWeather : 0) | (in.daylightCloses ? OverrideDaylight : 0) |
                (parkHolds(in) ? 0 : OverridePark));
        }

        uint8_t overrideStillApplies(uint8_t active, const GateInputs &in)
        {
            const bool weatherOut = in.weather != Safety::Verdict::Safe;
            if (weatherOut && ((active & OverrideWeather) == 0 || !weatherOverridable(in)))
                return 0; // a weather reason the person didn't confirm
            if (in.daylightCloses && (active & OverrideDaylight) == 0)
                return 0;
            return static_cast<uint8_t>((weatherOut ? OverrideWeather : 0) | (in.daylightCloses ? OverrideDaylight : 0));
        }

        uint8_t overrideBit(const char *name)
        {
            const std::string n = name != nullptr ? name : "";
            if (n == "mountNotParked")
                return OverridePark;
            if (n == "weatherNotSafe")
                return OverrideWeather;
            return n == "daylight" ? OverrideDaylight : 0;
        }

        GateRefusal gateCommand(Roof::Command command, const GateInputs &in, uint8_t overrides)
        {
            const bool parkSetAside = (overrides & OverridePark) != 0;
            switch (command)
            {
            case Roof::Command::Open:
            {
                const GateRefusal refusal = openRefusal(in, overrides);
                if (refusal != GateRefusal::None)
                    return refusal;
                if (in.stuckSwitch != 0)
                    return GateRefusal::SwitchCheck;
                return parkSetAside || parkHolds(in) ? GateRefusal::None : GateRefusal::MountNotParked;
            }
            case Roof::Command::Close:
            {
                // Spec 030: N.I.N.A.'s part set aside (Close only), the switches still count.
                const bool ninaAside = (overrides & OverrideNinaLost) != 0 && switchesClosed(in.fittedSwitches, in.switch1, in.switch2);
                return parkSetAside || ninaAside || parkHolds(in) ? GateRefusal::None : GateRefusal::MountNotParked;
            }
            default:
                return GateRefusal::None;
            }
        }

        namespace
        {
            // The relied-on reading is still fresh. Unbounded only without a
            // ride-through and with no time handed over (the demo, the tests).
            bool readingFresh(const GateInputs &in)
            {
                if (in.parkFreshUntilMs == 0)
                    return !in.ninaGrace;
                return in.nowMs <= in.parkFreshUntilMs;
            }
        } // namespace

        bool parkHoldsFresh(const GateInputs &in)
        {
            return (in.parked || in.ninaGrace) && readingFresh(in) && switchesClosed(in.fittedSwitches, in.switch1, in.switch2);
        }

        namespace
        {
            // Spec 047 (constitution 12.0.0, II): staleness alone never stops a
            // move; the main loop drops `parked` and `parkUnanswered` the moment
            // a fresh answer says "not parked". A ride-through handed over
            // without `parkUnanswered` (the demo, the tests) stays bounded.
            bool parkResultHolds(const GateInputs &in)
            {
                return in.parked || in.parkUnanswered || (in.ninaGrace && readingFresh(in));
            }
        } // namespace

        bool closeMayContinue(const GateInputs &in)
        {
            return parkResultHolds(in) && switchesClosed(in.fittedSwitches, in.switch1, in.switch2);
        }

        namespace
        {
            // The park interlock watches this move: a close, an open or a
            // calibration leg, except the moves a person (or the N.I.N.A.-lost
            // close) runs without it.
            bool parkWatched(const Roof::Status &status, bool parkOverridden, bool openParkOverridden)
            {
                // Spec 016: any calibration leg too. Until the person confirms the open
                // end, a leg may drive the roof the other way (a motor wired backwards).
                // Spec 027: an open too (the roof passes over the telescope either way).
                if (parkOverridden && status.state == Roof::State::Closing)
                    return false;
                // Spec 039 (constitution 10.0.0, II): a person's confirmed open past
                // the park; every other stop (weather, faults, Stop) still applies.
                if (openParkOverridden && status.state == Roof::State::Opening && !status.calibrating && !status.jogging)
                    return false;
                // Spec 033 (constitution 9.0.0, II): a person at the roof jogging it
                // toward closed watches the telescope themselves.
                if (status.jogging && status.target == Roof::End::Closed)
                    return false;
                return status.state == Roof::State::Closing || status.state == Roof::State::Opening || status.calibrating;
            }
        } // namespace

        bool interlockLost(const Roof::Status &status, const GateInputs &in, bool parkOverridden, bool ninaLost, bool openParkOverridden)
        {
            if (!parkWatched(status, parkOverridden, openParkOverridden))
                return false;
            if (ninaLost && status.state == Roof::State::Closing)
                return !switchesClosed(in.fittedSwitches, in.switch1, in.switch2);
            return !closeMayContinue(in);
        }

        bool movesOnStale(const Roof::Status &status, const GateInputs &in, bool parkOverridden, bool ninaLost, bool openParkOverridden)
        {
            if (!parkWatched(status, parkOverridden, openParkOverridden) || (ninaLost && status.state == Roof::State::Closing))
                return false;
            return closeMayContinue(in) && !parkHoldsFresh(in);
        }

        uint32_t interlockBits(const GateInputs &in)
        {
            uint32_t bits = 0;
            if (!parkResultHolds(in))
                bits |= in.ninaGrace ? InterlockNinaStale : InterlockParkResult;
            if (in.fittedSwitches >= 1 && in.switch1 != Contact::Closed)
                bits |= InterlockSwitch1;
            if (in.fittedSwitches >= 2 && in.switch2 != Contact::Closed)
                bits |= InterlockSwitch2;
            return bits;
        }

        uint32_t interlockRecordBits(const Roof::Status &status, const GateInputs &in)
        {
            const bool opening = status.state == Roof::State::Opening;
            return interlockBits(in) | (opening ? static_cast<uint32_t>(InterlockDuringOpen) : 0u);
        }

        bool openMustStop(const Roof::Status &status, const GateInputs &in, uint8_t overrides)
        {
            // Spec 019: a jog in either direction (the motor direction may be wrong).
            // A jog never carries an override.
            // Spec 033: a jog toward closed never (a close always wins, constitution III).
            if (status.jogging)
                return status.target == Roof::End::Open && openRefusal(in) != GateRefusal::None;
            return status.state == Roof::State::Opening && openRefusal(in, overrides) != GateRefusal::None;
        }

        GateRefusal openRefusal(const GateInputs &in, uint8_t overrides)
        {
            const bool weatherSetAside = (overrides & OverrideWeather) != 0 && weatherOverridable(in);
            if (in.weather != Safety::Verdict::Safe && !weatherSetAside)
                return GateRefusal::WeatherNotSafe;
            return in.daylightCloses && (overrides & OverrideDaylight) == 0 ? GateRefusal::Daylight : GateRefusal::None;
        }

        const char *gateRefusalName(GateRefusal refusal)
        {
            switch (refusal)
            {
            case GateRefusal::WeatherNotSafe:
                return "weatherNotSafe";
            case GateRefusal::MountNotParked:
                return "mountNotParked";
            case GateRefusal::Daylight:
                return "daylight";
            case GateRefusal::SwitchCheck:
                return "parkSwitchCheck";
            default:
                return "none";
            }
        }
    } // namespace RoofIo
} // namespace SQM
