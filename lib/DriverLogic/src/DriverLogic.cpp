#include "DriverLogic.h"

namespace SQM
{
    namespace Driver
    {
        Outputs outputsFor(Command command, uint8_t duty)
        {
            Outputs out;
            switch (command)
            {
            case Command::Forward:
                out.enable = true;
                out.rpwm = duty;
                break;
            case Command::Reverse:
                out.enable = true;
                out.lpwm = duty;
                break;
            case Command::Off:
            default:
                break;
            }
            return out;
        }

        Outputs physicalOutputs(const Outputs &logical, bool benchBuild)
        {
            if (benchBuild)
                return Outputs();
            return logical;
        }
    } // namespace Driver
} // namespace SQM
