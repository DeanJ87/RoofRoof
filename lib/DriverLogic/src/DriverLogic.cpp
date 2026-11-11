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
    } // namespace Driver
} // namespace SQM
