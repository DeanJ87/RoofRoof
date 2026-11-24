#include "RoofGates.h"

#include <cstdio>

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

        GateRefusal gateCommand(Roof::Command command, const GateInputs &in)
        {
            switch (command)
            {
            case Roof::Command::Open:
            {
                const GateRefusal refusal = openRefusal(in);
                return refusal == GateRefusal::None && in.stuckSwitch != 0 ? GateRefusal::SwitchCheck : refusal;
            }
            case Roof::Command::Close:
                return parkHolds(in) ? GateRefusal::None : GateRefusal::MountNotParked;
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

        bool closeMayContinue(const GateInputs &in)
        {
            return (in.parked || in.ninaGrace) && readingFresh(in) && switchesClosed(in.fittedSwitches, in.switch1, in.switch2);
        }

        bool interlockLost(const Roof::Status &status, const GateInputs &in)
        {
            // Spec 016: any calibration leg too. Until the person confirms the open
            // end, a leg may drive the roof the other way (a motor wired backwards).
            return (status.state == Roof::State::Closing || status.calibrating) && !closeMayContinue(in);
        }

        uint32_t interlockBits(const GateInputs &in)
        {
            uint32_t bits = 0;
            if (!in.parked && !in.ninaGrace)
                bits |= InterlockParkResult;
            else if (!readingFresh(in))
                bits |= InterlockNinaStale;
            if (in.fittedSwitches >= 1 && in.switch1 != Contact::Closed)
                bits |= InterlockSwitch1;
            if (in.fittedSwitches >= 2 && in.switch2 != Contact::Closed)
                bits |= InterlockSwitch2;
            return bits;
        }

        bool openMustStop(const Roof::Status &status, const GateInputs &in)
        {
            // Spec 019: a jog in either direction (the motor direction may be wrong).
            return (status.state == Roof::State::Opening || status.jogging) && openRefusal(in) != GateRefusal::None;
        }

        GateRefusal openRefusal(const GateInputs &in)
        {
            if (in.weather != Safety::Verdict::Safe)
                return GateRefusal::WeatherNotSafe;
            return in.daylightCloses ? GateRefusal::Daylight : GateRefusal::None;
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
