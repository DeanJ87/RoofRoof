#include "PinText.h"

#include <cstdio>

namespace SQM
{
    namespace Board
    {
        namespace
        {
            template <typename... Args> std::string format(const char *pattern, Args... args)
            {
                char buffer[200];
                std::snprintf(buffer, sizeof(buffer), pattern, args...);
                return buffer;
            }

            std::string errorText(const Issue &issue, const char *what, unsigned pin)
            {
                switch (issue.code)
                {
                case IssueCode::Missing:
                    return format("%s on GPIO%u: the ESP32 has no GPIO%u", what, pin, pin);
                case IssueCode::Flash:
                    return format("%s on GPIO%u: GPIO%u is a flash pin", what, pin, pin);
                case IssueCode::Psram:
                    return format("%s on GPIO%u: GPIO%u is used by the PSRAM on WROVER modules", what, pin, pin);
                case IssueCode::Uart:
                    return format("%s on GPIO%u: GPIO%u is the serial port used for the log and USB flashing", what, pin, pin);
                case IssueCode::Ethernet:
                    return format("%s on GPIO%u: GPIO%u is used by the WT32-ETH01's Ethernet", what, pin, pin);
                case IssueCode::Probe:
                    return format("%s on GPIO%u: GPIO%u is used to look for a WT32-ETH01 at start-up", what, pin, pin);
                case IssueCode::Header:
                    return format("%s on GPIO%u: the WT32-ETH01's header doesn't bring out GPIO%u", what, pin, pin);
                case IssueCode::InputOnly:
                    return format("%s on GPIO%u: GPIO%u is input only and can't drive an output", what, pin, pin);
                case IssueCode::NotAdc1:
                    return format(
                        "%s on GPIO%u: an analog input must be on ADC1 (GPIO32-39), ADC2 can't be read while WiFi is on", what, pin);
                case IssueCode::NoPullup:
                    return format("%s on GPIO%u: GPIO%u has no internal pull-up, so an open switch would float", what, pin, pin);
                case IssueCode::Shared:
                    return format("%s on GPIO%u: GPIO%u is already %s", what, pin, pin, functionLabel(issue.other));
                case IssueCode::EnStrap:
                    return format(
                        "EN on GPIO%u: EN can't be on a strapping pin or one the chip drives while it starts (0, 2, 5, 12, 14, 15)", pin);
                default:
                    return format("%s on GPIO%u: this preset's pins are fixed; choose Custom to change one", what, pin);
                }
            }

            std::string warningText(const char *what, unsigned pin, IssueCode code)
            {
                switch (code)
                {
                case IssueCode::Strap0:
                    return format(
                        "%s on GPIO%u: a strapping pin; held low at reset (a closed switch), the board starts in its download mode instead "
                        "of the firmware",
                        what,
                        pin);
                case IssueCode::Strap2:
                    return format(
                        "%s on GPIO%u: a strapping pin; it must be low or floating for USB flashing, a normal start ignores it", what, pin);
                case IssueCode::Strap5:
                    return format(
                        "%s on GPIO%u: a strapping pin (SDIO timing, unused here) that pulses while the chip starts; fine for a switch "
                        "input, or a PWM input while EN is held low",
                        what,
                        pin);
                case IssueCode::Strap12:
                    return format(
                        "%s on GPIO%u: a strapping pin that sets the flash voltage; held high at reset, a 3.3 V flash module won't start",
                        what,
                        pin);
                default:
                    return format(
                        "%s on GPIO%u: a strapping pin (low at reset only silences the boot log) that pulses while the chip starts; fine "
                        "for a switch input, or a PWM input while EN is held low by its pull-down",
                        what,
                        pin);
                }
            }
        } // namespace

        std::string issueText(const Issue &issue)
        {
            if (issue.function == Function::None && issue.code == IssueCode::BoardDetected)
                return boardDetectedText();
            if (issue.function == Function::None)
                return presetBoardText();
            const char *what = functionLabel(issue.function);
            const unsigned pin = issue.pin;
            return issue.error ? errorText(issue, what, pin) : warningText(what, pin, issue.code);
        }

        std::string boardDetectedText()
        {
            return "This board is a WT32-ETH01 (its Ethernet answered): choose a WT32-ETH01 board";
        }

        std::string presetBoardText()
        {
            return "This preset is for another board; choose Custom to change the board";
        }

        std::string unsetText()
        {
            return "No board chosen yet: choose the board and its pins";
        }

        std::string unreadableText()
        {
            return "The saved pin setup couldn't be read";
        }
    } // namespace Board
} // namespace SQM
