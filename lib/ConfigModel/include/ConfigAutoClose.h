#pragma once

// The automatic close's settings (Config::autoClose), kept apart from Config.h
// so that file stays within its size limit (spec 030).

#include <cstdint>

namespace SQM
{
    // The automatic close's policy (spec 014; specs 005 and 013 fixed these).
    // The defaults are the values those specs chose; lib/CloseSequenceLogic's
    // ClosePolicy clamps them again.
    struct AutoCloseConfig
    {
        uint32_t rearmMinutes = 10;       // the safe spell that ends a stand-down
        uint32_t parkRequestSeconds = 30; // between park requests
        uint32_t parkHoldSeconds = 4;     // the park must hold this long before the close
        uint32_t retryMinutes = 5;        // the back-off after a failed attempt or close
        uint32_t closeStartSeconds = 30;  // a waiting close must start within this
        uint32_t closeGuardSeconds = 30;  // added to the move's own timeout
        enum class NotStarted : uint8_t
        {
            Halt = 0,
            Retry = 1,
        };
        NotStarted notStarted = NotStarted::Halt;
        uint32_t remindMinutes = 30; // critical reminders and their throttle
        // An Alpaca or MQTT stop of a close in unsafe weather counts as a
        // person's stop (stand-down and its critical alert) instead of being refused.
        bool remoteStop = false;
        // Spec 030: the unsafe time (added up) before the automatic close
        // starts; rain or a failed rain sensor starts it at once. 0: no hold (spec 005's behaviour).
        uint32_t unsafeHoldSeconds = 60;
        // Spec 030 (owner option, off): with park.source nina or ninaAndSwitches,
        // unsafe weather and N.I.N.A. lost this long without a break, close
        // without N.I.N.A.'s park (the fitted switches must still read closed).
        bool ninaLostClose = false;
        uint32_t ninaLostMinutes = 5;
        // Spec 044 (owner setting): whether a person's Stop of the automatic
        // close lapses by itself. Never (the default, spec 030's behaviour): it
        // holds until the roof is closed, an Open moves it, a person releases it
        // or the safe spell. Rain: it lapses after stopHoldMinutes of rain (or a
        // failed rain sensor). Always: after stopHoldMinutes of any unsafe weather.
        enum class PersonStopLapse : uint8_t
        {
            Never = 0,
            Rain = 1,
            Always = 2,
        };
        PersonStopLapse personStopLapse = PersonStopLapse::Never;
        uint32_t stopHoldMinutes = 5;

        static constexpr uint32_t MIN_REARM_MINUTES = 5;
        static constexpr uint32_t MAX_REARM_MINUTES = 60;
        static constexpr uint32_t MIN_PARK_REQUEST_SECONDS = 10;
        static constexpr uint32_t MAX_PARK_REQUEST_SECONDS = 120;
        static constexpr uint32_t MIN_PARK_HOLD_SECONDS = 4;
        static constexpr uint32_t MAX_PARK_HOLD_SECONDS = 30;
        static constexpr uint32_t MIN_RETRY_MINUTES = 1;
        static constexpr uint32_t MAX_RETRY_MINUTES = 30;
        static constexpr uint32_t MIN_CLOSE_START_SECONDS = 25;
        static constexpr uint32_t MAX_CLOSE_START_SECONDS = 120;
        static constexpr uint32_t MIN_CLOSE_GUARD_SECONDS = 10;
        static constexpr uint32_t MAX_CLOSE_GUARD_SECONDS = 120;
        static constexpr uint32_t MIN_REMIND_MINUTES = 10;
        static constexpr uint32_t MAX_REMIND_MINUTES = 120;
        static constexpr uint32_t MIN_UNSAFE_HOLD_SECONDS = 0;
        static constexpr uint32_t MAX_UNSAFE_HOLD_SECONDS = 300;
        static constexpr uint32_t MIN_NINA_LOST_MINUTES = 1;
        static constexpr uint32_t MAX_NINA_LOST_MINUTES = 60;
        static constexpr uint32_t MIN_STOP_HOLD_MINUTES = 2;
        static constexpr uint32_t MAX_STOP_HOLD_MINUTES = 60;
    };
} // namespace SQM
