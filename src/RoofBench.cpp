#include "BenchMode.h"

#if ROOFROOF_BENCH

#include <Arduino.h>

#include "Logger.h"
#include "MotorDriver.h"
#include "RoofControl.h"
#include "RoofLocked.h"
#include "RoofPins.h"
#include "RoofReadings.h"
#include "RoofSettings.h"

// Bench mode (spec 011, contracts/bench-mode.md): only in the bench build. The
// simulated roof (lib/RoofPlantLogic) follows what the motor driver would
// apply (MotorDriver::logicalOutputs; the pins themselves stay low) and its
// Hall, supply and current replace the ADC's. Everything else is the standard
// firmware's. The real supply input is still read: a board that sees a 12 V
// supply is on a roof, and latches a fault instead of simulating one.

namespace SQM
{
    namespace
    {
        // The longest step the simulated roof takes at once: a late step is
        // the motion logic's to fault on, not a teleport.
        constexpr uint32_t MAX_PLANT_STEP_MS = 100;

        float realSupplyVolts()
        {
            uint32_t samples[RoofIo::ANALOG_SAMPLES];
            for (uint32_t &sample : samples)
                sample = analogReadMilliVolts(Pins::SUPPLY_SENSE);
            return RoofIo::supplyVolts(RoofIo::averageMillivolts(samples, RoofIo::ANALOG_SAMPLES));
        }
    } // namespace

    // Before the control task starts: the roof starts closed, and the guard
    // has a second of real supply readings, so no command can arrive before it
    // has decided.
    void RoofControl::benchStart()
    {
        benchPlant = RoofPlant::forSettings(Roof::settingsFromConfig(settings.roof), 0.0f);
        Logger::warn(TAG, "BENCH MODE: the roof is simulated and the motor is never driven (EN, RPWM and LPWM held low)");
        uint32_t now = millis();
        for (uint32_t waited = 0; waited <= RoofPlant::BenchSupplyGuard::CONFIRM_MS; waited += RoofIo::CONTROL_PERIOD_MS)
        {
            benchGuard.update(realSupplyVolts(), now);
            delay(RoofIo::CONTROL_PERIOD_MS);
            now = millis();
        }
        if (!benchGuard.update(realSupplyVolts(), now))
            return;
        raiseFault(RoofIo::FaultCause::BenchSupply, benchGuard.measured(), RoofPlant::BenchSupplyGuard::LIMIT_VOLTS);
        Logger::error(TAG, "Bench firmware on a powered board: nothing will move. Flash the standard firmware for a roof.");
    }

    void RoofControl::benchStep(StepInputs &in)
    {
        if (benchGuard.update(in.readings.supplyVolts, in.readings.nowMs))
            latchExternal(
                RoofIo::externalRecord(RoofIo::FaultCause::BenchSupply, benchGuard.measured(), RoofPlant::BenchSupplyGuard::LIMIT_VOLTS));
        const uint32_t elapsed = in.gapMs < MAX_PLANT_STEP_MS ? in.gapMs : MAX_PLANT_STEP_MS;
        benchPlant.drive(RoofPlant::outputOf(MotorDriver::logicalOutputs()), elapsed);
        in.readings.hallCounts = benchPlant.hallCounts();
        in.readings.supplyVolts = benchPlant.supplyNow();
        in.readings.motorAmps = settings.roof.currentSenseEnabled ? benchPlant.senseAmps() : NAN;
    }

    // New settings taken by the logic: the simulated roof follows them.
    void RoofControl::benchSettings()
    {
        RoofPlant::applySettings(benchPlant, Roof::settingsFromConfig(settings.roof));
    }
} // namespace SQM

#endif
