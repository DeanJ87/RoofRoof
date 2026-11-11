#pragma once

// The only code that writes the IBT-2's pins (constitution Principle I). The
// levels come from lib/DriverLogic. Spec 001 holds the driver off at boot;
// spec 003 drives it for the roof's control task and gives the watchdog a way
// to switch it off that the control task can't undo.

#include "DriverLogic.h"

namespace SQM
{
    namespace MotorDriver
    {
        // PWM on RPWM and LPWM (spec 003 research R11).
        constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
        constexpr uint8_t PWM_RESOLUTION_BITS = 8;

        // EN, RPWM and LPWM as outputs, all low. First thing in setup(): until
        // then the pins float and only the EN pull-down holds the driver off.
        void holdOff();

        // Attaches the PWM to RPWM and LPWM at duty 0 (EN stays low). After
        // holdOff(), before the first apply().
        bool begin();

        // Sets the pins to `outputs`. Switching on: the other direction's PWM
        // to 0, then this direction's, then EN; switching off: EN first. EN is
        // never set high while the watchdog's trip is set. Callers serialise
        // their calls (the roof's lock).
        void apply(const Driver::Outputs &outputs);

        // The watchdog (any task, including the esp_timer task): EN low at once,
        // and held low by apply() until clearTrip().
        void trip();
        bool tripped();
        // The control task, once the watchdog's fault is latched and the output is off.
        void clearTrip();

        // The watchdog timer, every check: EN can only go high while the last
        // beat is at most RoofIo::WATCHDOG_BEAT_LIMIT_MS old, so a stopped
        // watchdog keeps the driver off.
        void watchdogBeat();
        uint32_t watchdogBeatMs();

        // Lets EN go high: called once the control task and the watchdog both
        // run. Until then (and for good, if either failed to start) EN stays low.
        void arm();

        // A trip happened and its fault may not be saved yet (kept across a
        // reset that isn't a power cut). Cleared once the fault is in NVS.
        bool tripMarked();
        void clearTripMark();

        // EN is high (the driver is on).
        bool enabled();
    } // namespace MotorDriver
} // namespace SQM
