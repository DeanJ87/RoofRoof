#pragma once

// The latched fault as kept in NVS across restarts (spec 003 FR-011, FR-012,
// research R7; constitution I: a restart never clears the lockout). A record
// that can't be read back is a fault of its own, never "no fault".

#include <cstddef>
#include <cstdint>

#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // Who decided the fault. Everything but Logic is latched as
        // Roof::FaultKind::External with the values in data-model.md.
        enum class FaultCause : uint8_t
        {
            Logic,            // lib/RoofLogic's own fault kinds
            Watchdog,         // the control task stopped stepping with the driver on (gap ms / limit ms)
            ParkInterlock,    // the park confirmation was lost during a close (interlock bits / 0)
            StoredUnreadable, // the saved record couldn't be read back
            WatchdogStopped,  // the watchdog timer itself stopped with the driver on (beat age ms / limit ms)
        };

        struct FaultRecord
        {
            Roof::Fault fault;
            FaultCause cause = FaultCause::Logic;
        };

        constexpr size_t FAULT_RECORD_BYTES = 16;
        constexpr uint8_t FAULT_RECORD_VERSION = 1;

        enum class Decoded : uint8_t
        {
            None,       // nothing stored: no fault
            Fault,      // a fault to latch
            Unreadable, // something stored that doesn't check out: latch storedUnreadable
        };

        void encodeFault(const FaultRecord &record, uint8_t (&out)[FAULT_RECORD_BYTES]);
        // `length` 0 means no record. `out` is set for Fault and Unreadable.
        Decoded decodeFault(const uint8_t *data, size_t length, FaultRecord &out);

        // The record latched for a store that can't be read.
        FaultRecord storedUnreadableRecord();
        // An External fault with this cause and values.
        FaultRecord externalRecord(FaultCause cause, float measured, float limit);

        // "logic", "watchdog", "parkInterlock", "storedUnreadable".
        const char *causeName(FaultCause cause);
    } // namespace RoofIo
} // namespace SQM
