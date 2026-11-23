#pragma once

// The wiring check's verdicts (spec 023 FR-016, FR-018, FR-019, research R6,
// R7, R10): plain readings in, one result per check out. Nothing here, and
// nothing the firmware does for it, can move the roof.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "PinMap.h"

namespace SQM
{
    namespace Board
    {
        enum class Level : uint8_t
        {
            Pass,
            Warn,
            Fail,
            Skipped,
        };

        enum class CheckId : uint8_t
        {
            EnPulldown,
            Hall,
            Supply,
            ParkA,
            ParkB,
            CurrentR,
            CurrentL,
            Ethernet,
            Motion,
        };

        // EN read as an input with the internal pull-up (research R6).
        enum class EnProbe : uint8_t
        {
            NotRun, // the bench build: its pins stay low by contract
            Low,
            High,
            Busy, // the roof became busy between the start and the check
        };

        enum class SwitchLevel : uint8_t
        {
            Open,
            Closed,
            Unreadable, // chattering, or not read yet
        };

        // What is known about Ethernet: the running link on a board that has
        // it, or the probe on one that doesn't (research R7).
        struct EthernetFacts
        {
            enum class Mode : uint8_t
            {
                Running,
                Probed,
                SkippedPsram,
                SkippedPin, // `usedPin` is a roof function
            };
            Mode mode = Mode::SkippedPsram;
            // Running.
            bool started = false;
            bool phyFound = false;
            bool link = false;
            uint16_t speedMbps = 0;
            bool hasAddress = false;
            // Probed.
            bool clockSeen = false;
            bool idRead = false; // both ID registers answered (the turnaround was driven)
            uint16_t id1 = 0xFFFF;
            uint16_t id2 = 0xFFFF;
            // SkippedPin.
            uint8_t usedPin = 255;
        };

        struct WiringInputs
        {
            EnProbe en = EnProbe::NotRun;
            uint16_t hallCounts = 0;
            uint16_t hallRailLow = 30;    // Roof::HALL_RAIL_LOW_COUNTS
            uint16_t hallRailHigh = 4065; // Roof::HALL_RAIL_HIGH_COUNTS
            uint16_t hallMin = 100;       // the settings' range
            uint16_t hallMax = 3995;
            float supplyVolts = NAN;
            float supplyLimit = 11.0f;  // roof.minSupplyVolts
            uint8_t fittedSwitches = 0; // park.switches: 0, 1 (A) or 2 (A and B)
            SwitchLevel parkA = SwitchLevel::Unreadable;
            SwitchLevel parkB = SwitchLevel::Unreadable;
            bool currentSense = false; // roof.currentSenseEnabled
            float currentAmps[2] = {NAN, NAN};
            float currentMillivolts[2] = {NAN, NAN};
            EthernetFacts ethernet;
        };

        struct Check
        {
            CheckId id = CheckId::Motion;
            Level level = Level::Skipped;
            float value = NAN; // NaN: none
            const char *unit = "";
            std::string text;
        };

        struct WiringResult
        {
            std::vector<Check> checks;
            bool suggestWt32 = false; // the probe found a LAN8720 on a board set up without Ethernet
        };

        // The limits (research R10).
        constexpr float SUPPLY_ABSENT_VOLTS = 3.0f;
        constexpr float SUPPLY_MAX_VOLTS = 15.0f;
        constexpr float CURRENT_REST_AMPS = 1.0f;
        constexpr float CURRENT_REST_MILLIVOLTS = 80.0f; // with current sense off: about 1 A at 8.5 A/V through the 10k/20k divider

        WiringResult evaluate(const WiringInputs &in);

        const char *checkIdName(CheckId id);
        const char *levelName(Level level);
    } // namespace Board
} // namespace SQM
