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
        // A Hall level change (arriving at a magnet, a wrong magnet, leaving one)
        // counts only once the filtered reading has held it this long without a
        // break, and over at least HALL_CONFIRM_READINGS steps. Motor PWM noise
        // coupled into the sensor line reads as bursts of tens of milliseconds;
        // this outlasts them. The cost is overrun at an end: with the median's
        // two steps, about 260 ms, 4 mm at a 40 % creep and 10 mm at full speed
        // of 38 mm/s (the profile reaches an end at the slow duty).
        constexpr uint32_t HALL_PERSIST_MS = 200;
        constexpr uint8_t HALL_CONFIRM_READINGS = 3;
        // Back on the start magnet this soon after leaving it (and within the
        // leave time) counts as not yet left: noise as the reading crossed the
        // threshold. Longer away, it is a wrong magnet: the roof came back.
        constexpr uint32_t HALL_REENTRY_GRACE_MS = 1000;
        // The raw readings kept for the log of a fault (oldest first).
        constexpr uint8_t HALL_TRAIL_READINGS = 8;
        // A confirmed magnet is left only this far past its threshold, so a roof
        // parked on a threshold doesn't flicker.
        constexpr uint16_t HALL_HYSTERESIS_COUNTS = 40;
        // The SS49E through its divider never reaches these; a reading here is a
        // disconnected or shorted sensor. Low: the sensor's lowest output (about
        // 0.86 V on 5 V) through the 10k/20k divider reads about 650 counts, so
        // anything at or under 400 is a lost supply or ground pulled down by the
        // divider, never a magnet (field report: 100-160 at an end).
        constexpr uint16_t HALL_RAIL_LOW_COUNTS = 400;
        constexpr uint16_t HALL_RAIL_HIGH_COUNTS = 4065;
        // A rail is confirmed by HALL_RAIL_LATCH_READINGS rail readings among the
        // last HALL_RAIL_WINDOW_READINGS, consecutive or not: an intermittent
        // connector that alternates good and rail readings still faults, while a
        // single ADC spike doesn't.
        constexpr uint8_t HALL_RAIL_LATCH_READINGS = 3;
        constexpr uint8_t HALL_RAIL_WINDOW_READINGS = 8;
        // Spec 027: a Hall rail fault latched at rest clears after this long of
        // readings off the rails, still at rest (constitution 7.0.0, I).
        constexpr uint32_t HALL_REST_CLEAR_MS = 60000;
        // Spec 027: a rail reading at rest this soon after start-up is not judged
        // (a power-up transient latched "Hall sensor at a rail" at every boot).
        constexpr uint32_t HALL_BOOT_SETTLE_MS = 500;
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

        // Spec 019: a jog stops when it hasn't been renewed for JOG_HOLD_MS of
        // step time (the page renews it every JOG_KEEPALIVE_MS while the button
        // is held), and after JOG_MAX_MS at most.
        constexpr uint32_t JOG_HOLD_MS = 500;
        constexpr uint32_t JOG_KEEPALIVE_MS = 150;
        constexpr uint32_t JOG_MAX_MS = 30000;

        // Spec 030 (research R6): a blind close judges the supply sag only after
        // the motor's start-up surge, and stops on a sag held this long.
        constexpr uint32_t BLIND_SURGE_MS = 1000;
        constexpr uint32_t BLIND_STALL_MS = 500;

        // Spec 036 (constitution 8.1.0, I): an end magnet read early (before
        // roof.endPlausiblePercent of the travel) holds the move at the slow duty
        // toward it. It is accepted once the reading has lasted EARLY_END_PERSIST_MS
        // and either the estimate reaches the plausible point or the reading is
        // clearly at the magnet: past its threshold by half the gap between the
        // two thresholds, for EARLY_END_PERSIST_MS. Not accepted within
        // EARLY_END_HOLD_MS, or the reading lost, is the EndTooEarly fault.
        constexpr uint32_t EARLY_END_PERSIST_MS = 1000;
        constexpr uint32_t EARLY_END_HOLD_MS = 5000;
        constexpr uint32_t END_OVERRUN_MIN_MS = 3000;
        // Spec 036: the movement check ignores the start-up surge (as the blind
        // close does) and counts a Hall change this large as movement.
        constexpr uint16_t MOTION_HALL_COUNTS = 60;

        // Whether a supply reading can be believed: finite and at most MAX_SUPPLY_READING_VOLTS.
        bool supplyReadable(float volts);
        // Whether a current reading can be believed: finite and at least MIN_CURRENT_READING_AMPS.
        bool currentReadable(float amps);

        // How the roof moves. Defaults match RoofConfig's.
        struct Settings
        {
            uint32_t travelMm = 1850;
            float speedMmPerSecond = 38.0f; // at 100% duty; scaled by the duty (research R2)
            // At the slow duty, as a calibration run measured it; 0 = not measured
            // (the full speed scaled by the slow duty). Affine in between (MotionProfile).
            float slowSpeedMmPerSecond = 0.0f;
            // The slow duty it was measured at: used only while that is still the slow duty.
            uint8_t slowSpeedDutyPercent = 0;
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
            // Spec 023 review: the analog setup that scales the readings passes its
            // checks (Config's analogUsable); false refuses every move.
            bool analogUsable = true;
            // Spec 030: the blind close is allowed, and its stall threshold.
            bool blindClose = true;
            float blindSagVolts = 2.0f;
            // Spec 036: an end magnet read before the estimate has covered this
            // percentage of the travel toward it is early (RoofControllerEnd.cpp).
            uint8_t endPlausiblePercent = 80;
            // Spec 036: a move from a known position that has driven this percentage of
            // its expected time (and at least END_OVERRUN_MIN_MS more) without its magnet
            // stops (NoEndMagnet), before the hard timeout. 0 is off (tests that isolate
            // another limit; the stored setting is 105-200).
            uint8_t endOverrunPercent = 115;
            // Spec 036: driving shows movement by a supply sag of at least this much
            // (held CONFIRM_MS) or a Hall change of MOTION_HALL_COUNTS within
            // motionCheckMs (twice that for a calibration leg or jog); 0 = off. The
            // logic's default is off (a bare Settings drives a model with no supply
            // sag); the stored setting's default is RoofConfig's 0.2 V.
            float motionSagVolts = 0.0f;
            uint32_t motionCheckMs = 10000;
        };

        // Whether `settings` are within the ranges RoofConfig documents. The
        // controller refuses every move while its settings are not, so a bad value
        // can never divide by zero or overflow a time.
        bool validSettings(const Settings &settings);

        // The stored settings in the logic's units.
        Settings settingsFromConfig(const RoofConfig &config);
    } // namespace Roof
} // namespace SQM
