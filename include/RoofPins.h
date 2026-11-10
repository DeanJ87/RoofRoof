#pragma once

// The roof controller's pins (constitution, Platform Constraints; wiring in
// docs/hardware/wiring.md). Fixed by the hardware: none of these are settings.

#include <cstdint>

namespace SQM
{
    namespace Pins
    {
        // IBT-2 (BTS7960) H-bridge. Never RPWM and LPWM together; EN low = off.
        constexpr uint8_t RPWM = 25;
        constexpr uint8_t LPWM = 26;
        constexpr uint8_t ENABLE = 27; // R_EN and L_EN joined, 10 kOhm pull-down to GND

        // Analog inputs, all on ADC1 (works while WiFi is on).
        constexpr uint8_t CURRENT_R = 34;    // R_IS through a 10k/20k divider (may not be fitted)
        constexpr uint8_t CURRENT_L = 35;    // L_IS, the same
        constexpr uint8_t SUPPLY_SENSE = 36; // 12 V supply through 100k/22k
        constexpr uint8_t HALL_SENSOR = 39;  // SS49E linear Hall output

        // Optional switch inputs (internal pull-ups on 32/33 and 4).
        constexpr uint8_t PARK_A = 32;
        constexpr uint8_t PARK_B = 33;
        constexpr uint8_t RAIN = 4;
    } // namespace Pins
} // namespace SQM
