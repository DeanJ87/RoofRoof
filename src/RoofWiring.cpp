#include <Arduino.h>

#include "RoofControl.h"
#include "RoofLocked.h"
#include "RoofReadings.h"
#include "RoofSettings.h"

// Spec 023: what the wiring check reads from the roof's inputs. Only inputs:
// nothing here can drive a pin.

namespace SQM
{
    namespace
    {
        float averagePinMillivolts(uint8_t pin)
        {
            uint32_t samples[RoofIo::CURRENT_SAMPLES];
            for (uint32_t &sample : samples)
                sample = analogReadMilliVolts(pin);
            return RoofIo::averageMillivolts(samples, RoofIo::CURRENT_SAMPLES);
        }

        Board::SwitchLevel levelOf(RoofIo::Contact contact)
        {
            switch (contact)
            {
            case RoofIo::Contact::Open:
                return Board::SwitchLevel::Open;
            case RoofIo::Contact::Closed:
                return Board::SwitchLevel::Closed;
            default:
                return Board::SwitchLevel::Unreadable;
            }
        }
    } // namespace

    void RoofControl::wiringReadings(Board::WiringInputs &in) const
    {
        if (lock == nullptr || !started.load())
            return;
        // Under the lock: the control task's own ADC reads can't interleave.
        RoofLocked held(lock);
        in.hallCounts = controller.status().hallCounts;
        in.hallRailLow = Roof::HALL_RAIL_LOW_COUNTS;
        in.hallRailHigh = Roof::HALL_RAIL_HIGH_COUNTS;
        in.hallMin = RoofConfig::MIN_HALL_COUNTS;
        in.hallMax = RoofConfig::MAX_HALL_COUNTS;
        in.supplyVolts = supplyVolts;
        in.supplyLimit = settings.roof.minSupplyVolts;
        in.fittedSwitches = settings.fittedSwitches;
        in.parkA = levelOf(gates.switch1);
        in.parkB = levelOf(gates.switch2);
        in.currentSense = settings.roof.currentSenseEnabled;
        const uint8_t sensePins[2] = {pins.currentR, pins.currentL};
        for (size_t side = 0; side < 2; ++side)
        {
            const float mv = averagePinMillivolts(sensePins[side]);
            in.currentMillivolts[side] = mv;
            in.currentAmps[side] = mv / 1000.0f * RoofIo::CURRENT_DIVIDER * settings.roof.currentAmpsPerVolt;
        }
    }
} // namespace SQM
