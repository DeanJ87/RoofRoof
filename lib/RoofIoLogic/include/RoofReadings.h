#pragma once

// Turning the roof's ADC samples into the values lib/RoofLogic takes (spec
// 003 FR-001 to FR-003, research R4, R5), and the hardware layer's fixed
// timing. Pure: the firmware takes the samples, this decides what they mean.

#include <cstdint>

#include "RoofSettings.h"

namespace SQM
{
    namespace RoofIo
    {
        // The control task's period: the rate spec 002's Hall confirmation and
        // step-gap limit assume.
        constexpr uint32_t CONTROL_PERIOD_MS = 20;
        // How often the watchdog looks, and the gap it allows while the driver
        // is on (spec 002's own step-gap limit): EN drops within 110 ms of the
        // last step.
        constexpr uint32_t WATCHDOG_CHECK_MS = 10;
        constexpr uint32_t WATCHDOG_LIMIT_MS = Roof::MAX_STEP_GAP_MS;
        // A live update after a state or fault change, at most this often.
        constexpr uint32_t STATUS_BROADCAST_MIN_MS = 250;

        // Samples per control step: the Hall's median, the averaged supply and current.
        constexpr uint8_t HALL_SAMPLES = 5;
        constexpr uint8_t ANALOG_SAMPLES = 4;
        // The current senses are chopped by the 20 kHz PWM: more samples,
        // spread over several PWM periods, average them (MotionGuards.h).
        constexpr uint8_t CURRENT_SAMPLES = 8;
        // The most samples the helpers below take.
        constexpr uint8_t MAX_SAMPLES = 9;

        // The pin table's dividers (constitution): 100k/22k on the supply,
        // 10k/20k on each current-sense output.
        constexpr float SUPPLY_DIVIDER = 122.0f / 22.0f;
        constexpr float CURRENT_DIVIDER = 30.0f / 20.0f;

        // The median of `count` raw ADC counts (the lower middle for an even
        // count). Keeps a rail reading a rail while rejecting a single spike.
        // 0 for a count of 0; only the first MAX_SAMPLES are used.
        uint16_t medianCounts(const uint16_t *samples, uint8_t count);

        // The mean of `count` millivolt samples; NaN for a count of 0.
        float averageMillivolts(const uint32_t *samples, uint8_t count);

        // The supply voltage for `pinMillivolts` at GPIO36; NaN in, NaN out.
        float supplyVolts(float pinMillivolts);

        // The motor current: the larger of the two sense outputs (only one
        // carries current in a direction), through the divider and the
        // board's amps per volt. NaN only if both readings are NaN.
        float motorAmps(float rightMillivolts, float leftMillivolts, float ampsPerVolt);
    } // namespace RoofIo
} // namespace SQM
