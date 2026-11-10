#pragma once

// The only code that writes the IBT-2's pins (constitution Principle I). The
// levels come from lib/DriverLogic. Spec 001 only holds the driver off; spec
// 003 adds driving it for the motion task.

namespace SQM
{
    namespace MotorDriver
    {
        // EN, RPWM and LPWM as outputs, all low. First thing in setup(): until
        // then the pins float and only the EN pull-down holds the driver off.
        void holdOff();
    } // namespace MotorDriver
} // namespace SQM
