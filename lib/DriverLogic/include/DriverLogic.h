#pragma once

// What the IBT-2 (BTS7960) H-bridge's inputs must be for each command. The one
// place pin levels are decided (constitution Principle I): src/MotorDriver.cpp
// only applies them. Spec 002 adds the motion that chooses commands.

#include <cstdint>

namespace SQM
{
    namespace Driver
    {
        // What the driver is asked to do.
        enum class Command : uint8_t
        {
            Off,     // EN low: the bridge is off and the motor coasts (the worm holds the roof)
            Forward, // RPWM at the duty, LPWM low
            Reverse, // LPWM at the duty, RPWM low
        };

        // The levels for EN (R_EN and L_EN joined) and the two PWM inputs.
        struct Outputs
        {
            bool enable = false;
            uint8_t rpwm = 0; // duty 0-255
            uint8_t lpwm = 0; // duty 0-255
        };

        // The outputs for `command` at `duty`. Never both PWM above 0; anything
        // but Forward or Reverse is Off.
        Outputs outputsFor(Command command, uint8_t duty);

        // What may reach the pins for `logical` (spec 011 FR-021): `logical`
        // itself, except in a bench build, where every level is low whatever
        // was asked (EN off, both PWM 0). Every pin write goes through this.
        Outputs physicalOutputs(const Outputs &logical, bool benchBuild);
    } // namespace Driver
} // namespace SQM
