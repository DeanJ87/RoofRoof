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
        float realSupplyVolts()
        {
            uint32_t samples[RoofIo::ANALOG_SAMPLES];
            for (uint32_t &sample : samples)
                sample = analogReadMilliVolts(Pins::SUPPLY_SENSE);
            return RoofIo::supplyVolts(RoofIo::averageMillivolts(samples, RoofIo::ANALOG_SAMPLES));
        }
    } // namespace

    // Before the control task starts: the guard has a second of real supply
    // readings, so no command can arrive before it has decided. The roof
    // starts half-way, so it reads unknown (the Dome: an error) until a person
    // moves it: a bench board that somehow ends up on a roof never starts by
    // reporting a closed roof.
    void RoofControl::benchStart()
    {
        const Roof::Settings roof = Roof::settingsFromConfig(settings.roof);
        benchPlant = RoofPlant::forSettings(roof, static_cast<float>(roof.travelMm) / 2.0f, settings.roof.minRunningAmps);
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
        RoofPlant::BenchStepIn step;
        step.real = in.readings;
        step.gapMs = in.gapMs;
        step.logical = MotorDriver::logicalOutputs();
        step.currentSense = settings.roof.currentSenseEnabled;
        const RoofPlant::BenchStepOut out = RoofPlant::benchStep(benchGuard, benchPlant, step);
        if (out.latch)
            benchLatch();
        in.readings = out.readings;
    }

    void RoofControl::benchLatch()
    {
        latchExternal(
            RoofIo::externalRecord(RoofIo::FaultCause::BenchSupply, benchGuard.measured(), RoofPlant::BenchSupplyGuard::LIMIT_VOLTS));
    }

    // New settings taken by the logic: the simulated roof follows them.
    void RoofControl::benchSettings()
    {
        RoofPlant::applySettings(benchPlant, Roof::settingsFromConfig(settings.roof), settings.roof.minRunningAmps);
    }
} // namespace SQM

#endif
