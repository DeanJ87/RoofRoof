#pragma once

// The pins in effect (spec 023): read from the settings once at boot and
// checked by lib/BoardLogic's rules. A set that passes is handed to
// MotorDriver, which drives EN and both PWM low before anything else; one
// that doesn't drives nothing (FR-010, FR-011, constitution I).

#include "Config.h"
#include "PinRules.h"

namespace SQM
{
    namespace PinSetup
    {
        // setup(), straight after the settings are read. `storedRejected`:
        // settings were stored but couldn't be read (Config::load).
        void begin(const Config &config, bool storedRejected);

        // After begin(), any task.
        bool valid();
        const Board::PinMap &running();
        // The rules' report on the pins in effect, and why they aren't valid.
        const Board::BootPins &boot();
        bool psram();
        Board::Facts facts();
        // The board in effect has Ethernet (the WT32-ETH01). Also with an
        // invalid set: holding the motor pins never touches the board's Ethernet pins.
        bool ethernetBoard();
        // FR-034: with no board chosen (a new device) or unreadable settings,
        // the board the boot probe found (a LAN8720 answered: the WT32-ETH01),
        // before any pin is touched. Probed: false when it couldn't run (PSRAM).
        bool boardProbed();
        Board::BoardId detectedBoard();
        // The board in effect: the chosen one, or the detected one when none is.
        Board::BoardId board();
    } // namespace PinSetup
} // namespace SQM
