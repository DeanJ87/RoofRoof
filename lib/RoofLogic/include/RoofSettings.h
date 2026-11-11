#pragma once

// The motion logic's settings, in its own units (spec 002, data-model.md), and
// the fixed values that describe the ADC, the driver and the control loop
// rather than the roof (research R8).

#include <cstdint>

#include "Config.h"
#include "DriverLogic.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace Roof
    {
        // Driver off between any two driven periods, so a direction change always
        // goes through stop first (constitution I).
        constexpr uint32_t DEAD_TIME_MS = 500;
        // Equal Hall readings in a row before a zone change counts: rejects single
        // ADC spikes; three steps at 50 Hz is about 1 mm at the slow speed.
        constexpr uint8_t HALL_CONFIRM_READINGS = 3;
        // A confirmed magnet is left only this far past its threshold, so a roof
        // parked on a threshold doesn't flicker.
        constexpr uint16_t HALL_HYSTERESIS_COUNTS = 40;
        // The SS49E through its divider never reaches these; a reading here is a
        // disconnected or shorted sensor.
        constexpr uint16_t HALL_RAIL_LOW_COUNTS = 30;
        constexpr uint16_t HALL_RAIL_HIGH_COUNTS = 4065;
        // A rail is confirmed by HALL_RAIL_LATCH_READINGS rail readings among the
        // last HALL_RAIL_WINDOW_READINGS, consecutive or not: an intermittent
        // connector that alternates good and rail readings still faults, while a
        // single ADC spike doesn't.
        constexpr uint8_t HALL_RAIL_LATCH_READINGS = 3;
        constexpr uint8_t HALL_RAIL_WINDOW_READINGS = 8;
        // Over-current and a low supply must last this long while driving: longer
        // than the motor's start-up surge, far shorter than a stall that heats it.
        constexpr uint32_t CONFIRM_MS = 250;
        // The longest gap between control steps while driving (spec 003 steps every
        // 20 ms or faster); longer means the motor ran unsupervised.
        constexpr uint32_t MAX_STEP_GAP_MS = 100;
        // A supply reading above this is not a 12 V system's: a wiring or ADC fault,
        // treated as unreadable (the supply minimum setting tops out at 15 V).
        constexpr float MAX_SUPPLY_READING_VOLTS = 16.0f;
        // The IBT-2's current outputs can't go negative; below this (beyond ADC
        // offset noise) the reading is broken, treated as unreadable.
        constexpr float MIN_CURRENT_READING_AMPS = -0.5f;

        // Spec 016: the range of a calibration leg's limit.
        constexpr uint32_t MIN_CALIBRATION_LEG_MS = 1000;
        constexpr uint32_t MAX_CALIBRATION_LEG_MS = 600000;

        // Whether a supply reading can be believed: finite and at most MAX_SUPPLY_READING_VOLTS.
        bool supplyReadable(float volts);
        // Whether a current reading can be believed: finite and at least MIN_CURRENT_READING_AMPS.
        bool currentReadable(float amps);

        // How the roof moves. Defaults match RoofConfig's.
        struct Settings
        {
            uint32_t travelMm = 1850;
            float speedMmPerSecond = 38.0f; // at 100% duty; scaled by the duty (research R2)
            uint32_t timeoutMarginMs = 15000;
            uint32_t leaveMagnetMs = 10000;
            uint32_t rampUpMs = 2000;
            uint32_t rampDownMs = 3000;
            uint8_t topDutyPercent = 100;
            uint8_t slowDutyPercent = 40;
            Driver::Command openDirection = Driver::Command::Forward;
            End hallHighEnd = End::Open;
            uint16_t hallHighCounts = 3000;
            uint16_t hallLowCounts = 1100;
            bool currentSenseEnabled = false;
            float maxCurrentAmps = 10.0f;
            float minSupplyVolts = 11.0f;
        };

        // Whether `settings` are within the ranges RoofConfig documents. The
        // controller refuses every move while its settings are not, so a bad value
        // can never divide by zero or overflow a time.
        bool validSettings(const Settings &settings);

        // The stored settings in the logic's units.
        Settings settingsFromConfig(const RoofConfig &config);
    } // namespace Roof
} // namespace SQM
