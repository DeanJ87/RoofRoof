#pragma once

// Which pin maps the device may use (spec 023 FR-005, FR-006, research R2):
// errors refuse a map, warnings explain a strapping pin. The same check runs
// at a save and at boot; at boot a map with any error drives nothing
// (bootPins, FR-011).

#include <string>
#include <vector>

#include "PinMap.h"

namespace SQM
{
    namespace Board
    {
        enum class IssueCode : uint8_t
        {
            // Errors.
            Missing,        // no such GPIO
            Flash,          // 6-11
            Psram,          // 16, 17 on a chip with PSRAM
            Uart,           // 1, 3
            Ethernet,       // the board's Ethernet
            Header,         // not brought out by the board
            InputOnly,      // an output on 34-39
            NotAdc1,        // an analog input off 32-39
            NoPullup,       // a park switch on 34-39
            Shared,         // two functions on one pin
            EnStrap,        // EN on a strap or a pin driven during boot
            PresetMismatch, // a named preset's pins changed
            // Warnings.
            Strap0,
            Strap2,
            Strap5,
            Strap12,
            Strap15,
        };

        struct Issue
        {
            Function function = Function::None;
            uint8_t pin = 255;
            IssueCode code = IssueCode::Missing;
            bool error = true;
            Function other = Function::None; // the function already on the pin (Shared)
            std::string text;                // English (the device catalogue)
        };

        struct PinReport
        {
            std::vector<Issue> issues;
            bool valid() const;
            // The first error, or nullptr.
            const Issue *firstError() const;
        };

        const char *issueCodeName(IssueCode code);

        // Every error and warning for `map` on a board with `facts`. Unset
        // (no board chosen) has none: it uses no pin, and boots driving nothing.
        PinReport check(const PinMap &map, const Facts &facts);

        // The boot decision (FR-010, FR-011): `storedUnreadable` when the
        // saved settings existed but couldn't be read.
        struct BootPins
        {
            bool valid = false;
            PinReport report;
            std::string reason;                // why not valid ("" when valid)
            std::vector<uint8_t> holdAsInputs; // when not valid: the candidate motor pins to leave as inputs
        };
        BootPins bootPins(const PinMap &map, const Facts &facts, bool storedUnreadable);
    } // namespace Board
} // namespace SQM
