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
                std::snprintf(text, sizeof(text), "safety inputs not updated for %u.%u s > %u s", static_cast<unsigned>(ageMs / 1000),
                              static_cast<unsigned>(ageMs % 1000 / 100), static_cast<unsigned>(SAFETY_HANDOVER_STALE_MS / 1000));
            else
                std::snprintf(text, sizeof(text), "safety inputs not evaluated yet");
            stale.weather = Safety::Verdict::Unknown;
            stale.weatherText = text;
            stale.parked = false;
            stale.parkText = text;
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
                return in.weather == Safety::Verdict::Safe ? GateRefusal::None : GateRefusal::WeatherNotSafe;
            case Roof::Command::Close:
                return parkHolds(in) ? GateRefusal::None : GateRefusal::MountNotParked;
            default:
                return GateRefusal::None;
            }
        }

        bool interlockLost(const Roof::Status &status, const GateInputs &in)
        {
            return status.state == Roof::State::Closing && !parkHolds(in);
        }

        uint32_t interlockBits(const GateInputs &in)
        {
            uint32_t bits = 0;
            if (!in.parked)
                bits |= InterlockParkResult;
            if (in.fittedSwitches >= 1 && in.switch1 != Contact::Closed)
                bits |= InterlockSwitch1;
            if (in.fittedSwitches >= 2 && in.switch2 != Contact::Closed)
                bits |= InterlockSwitch2;
            return bits;
        }

        bool openMustStop(const Roof::Status &status, const GateInputs &in)
        {
            return status.state == Roof::State::Opening && in.weather != Safety::Verdict::Safe;
        }

        const char *gateRefusalName(GateRefusal refusal)
        {
            switch (refusal)
            {
            case GateRefusal::WeatherNotSafe:
                return "weatherNotSafe";
            case GateRefusal::MountNotParked:
                return "mountNotParked";
            default:
                return "none";
            }
        }
    } // namespace RoofIo
} // namespace SQM
