#include "PinRules.h"

#include "PinText.h"

namespace SQM
{
    namespace Board
    {
        namespace
        {
            constexpr const char *CODE_NAMES[] = {
                "missing",
                "flash",
                "psram",
                "uart",
                "ethernet",
                "header",
                "inputOnly",
                "notAdc1",
                "noPullup",
                "shared",
                "enStrap",
                "presetMismatch",
                "strap0",
                "strap2",
                "strap5",
                "strap12",
                "strap15"};

            bool isOutput(Function function)
            {
                return function == Function::Rpwm || function == Function::Lpwm || function == Function::Enable;
            }

            bool isAnalog(Function function)
            {
                return function == Function::CurrentR || function == Function::CurrentL || function == Function::Supply ||
                       function == Function::Hall;
            }

            bool isPark(Function function)
            {
                return function == Function::ParkA || function == Function::ParkB;
            }

            // EN must be quiet from reset on (research R2).
            constexpr uint64_t EN_FORBIDDEN = gpioBit(0) | gpioBit(2) | gpioBit(5) | gpioBit(12) | gpioBit(14) | gpioBit(15);

            void add(PinReport &report, Function function, uint8_t pin, IssueCode code, Function other = Function::None)
            {
                Issue issue;
                issue.function = function;
                issue.pin = pin;
                issue.code = code;
                issue.error = code < IssueCode::Strap0;
                issue.other = other;
                issue.text = issueText(issue);
                report.issues.push_back(issue);
            }

            // Whether the pin can be used at all on this board: the first reason it can't.
            bool placeError(const Facts &f, uint8_t pin, IssueCode &code)
            {
                if (!Facts::has(f.exists, pin))
                    code = IssueCode::Missing;
                else if (Facts::has(f.flash, pin))
                    code = IssueCode::Flash;
                else if (Facts::has(f.psram, pin))
                    code = IssueCode::Psram;
                else if (Facts::has(f.uart, pin))
                    code = IssueCode::Uart;
                else if (Facts::has(f.ethernet, pin))
                    code = IssueCode::Ethernet;
                else if (f.header != 0 && !Facts::has(f.header, pin))
                    code = IssueCode::Header;
                else
                    return false;
                return true;
            }

            // What the function needs of the pin.
            bool roleError(const Facts &f, Function function, uint8_t pin, IssueCode &code)
            {
                if (isOutput(function) && Facts::has(f.inputOnly, pin))
                    code = IssueCode::InputOnly;
                else if (function == Function::Enable && Facts::has(EN_FORBIDDEN, pin))
                    code = IssueCode::EnStrap;
                else if (isAnalog(function) && !Facts::has(f.adc1, pin))
                    code = IssueCode::NotAdc1;
                else if (isPark(function) && Facts::has(f.inputOnly, pin))
                    code = IssueCode::NoPullup;
                else
                    return false;
                return true;
            }

            bool strapWarning(const Facts &f, uint8_t pin, IssueCode &code)
            {
                if (!Facts::has(f.straps, pin))
                    return false;
                code = pin == 0    ? IssueCode::Strap0
                       : pin == 2  ? IssueCode::Strap2
                       : pin == 5  ? IssueCode::Strap5
                       : pin == 12 ? IssueCode::Strap12
                                   : IssueCode::Strap15;
                return true;
            }

            void checkPresetMatch(const PinMap &map, PinReport &report)
            {
                if (!isNamed(map.preset))
                    return;
                const PinMap expected = presetMap(map.preset);
                if (map.board != expected.board)
                    add(report, Function::None, 255, IssueCode::PresetMismatch);
                for (size_t i = 0; i < FUNCTION_COUNT; ++i)
                {
                    const auto function = static_cast<Function>(i);
                    if (map.pin(function) != expected.pin(function))
                        add(report, function, map.pin(function), IssueCode::PresetMismatch);
                }
            }

            void checkShared(const PinMap &map, PinReport &report)
            {
                for (size_t j = 1; j < FUNCTION_COUNT; ++j)
                    for (size_t i = 0; i < j; ++i)
                    {
                        const auto first = static_cast<Function>(i);
                        const auto second = static_cast<Function>(j);
                        if (map.pin(first) == map.pin(second))
                        {
                            add(report, second, map.pin(second), IssueCode::Shared, first);
                            break; // one report per function
                        }
                    }
            }
        } // namespace

        bool PinReport::valid() const
        {
            return firstError() == nullptr;
        }

        const Issue *PinReport::firstError() const
        {
            for (const Issue &issue : issues)
                if (issue.error)
                    return &issue;
            return nullptr;
        }

        const char *issueCodeName(IssueCode code)
        {
            const auto index = static_cast<size_t>(code);
            return index < sizeof(CODE_NAMES) / sizeof(CODE_NAMES[0]) ? CODE_NAMES[index] : "";
        }

        PinReport check(const PinMap &map, const Facts &facts)
        {
            PinReport report;
            if (map.preset == Preset::Unset)
                return report; // no pins in use, nothing to check (bootPins drives nothing)
            checkPresetMatch(map, report);
            for (size_t i = 0; i < FUNCTION_COUNT; ++i)
            {
                const auto function = static_cast<Function>(i);
                const uint8_t pin = map.pin(function);
                IssueCode code;
                if (placeError(facts, pin, code) || roleError(facts, function, pin, code) || strapWarning(facts, pin, code))
                    add(report, function, pin, code);
            }
            checkShared(map, report);
            return report;
        }

        BootPins bootPins(const PinMap &map, const Facts &facts, bool storedUnreadable)
        {
            BootPins boot;
            boot.report = check(map, facts);
            const Issue *error = boot.report.firstError();
            const bool unset = map.preset == Preset::Unset;
            boot.valid = !storedUnreadable && !unset && error == nullptr;
            if (boot.valid)
                return boot;
            boot.reason = storedUnreadable ? unreadableText() : unset ? unsetText() : error->text;
            // Every preset's motor pins and the saved ones: an input with no
            // pull is a pin's state after reset, so it can't drive anything.
            // Never a pin the chip or the board needs (research R5).
            const PinMap candidates[] = {map, presetMap(Preset::Devkit), presetMap(Preset::Wt32CarrierV1)};
            for (const PinMap &candidate : candidates)
                for (Function function : {Function::Rpwm, Function::Lpwm, Function::Enable})
                {
                    const uint8_t pin = candidate.pin(function);
                    IssueCode code;
                    bool listed = false;
                    for (uint8_t held : boot.holdAsInputs)
                        listed = listed || held == pin;
                    if (!listed && !placeError(facts, pin, code))
                        boot.holdAsInputs.push_back(pin);
                }
            return boot;
        }
    } // namespace Board
} // namespace SQM
