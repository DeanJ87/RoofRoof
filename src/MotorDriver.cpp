#include "MotorDriver.h"

#include <Arduino.h>

#include "DriverLogic.h"
#include "RoofPins.h"

namespace SQM
{
    namespace MotorDriver
    {
        void holdOff()
        {
            const Driver::Outputs off = Driver::outputsFor(Driver::Command::Off, 0);
            // EN first: with it low the bridge ignores the PWM inputs. Each
            // level is set before the pin becomes an output, so it never
            // drives high for an instant.
            digitalWrite(Pins::ENABLE, off.enable ? HIGH : LOW);
            pinMode(Pins::ENABLE, OUTPUT);
            digitalWrite(Pins::RPWM, off.rpwm > 0 ? HIGH : LOW);
            pinMode(Pins::RPWM, OUTPUT);
            digitalWrite(Pins::LPWM, off.lpwm > 0 ? HIGH : LOW);
            pinMode(Pins::LPWM, OUTPUT);
        }
    } // namespace MotorDriver
} // namespace SQM
