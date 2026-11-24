#include <Arduino.h>

#include "RoofControl.h"
#include "Logger.h"
#include "MotionGuards.h"
#include "RoofLocked.h"
#include "RoofReadings.h"
#include "RoofSettings.h"
#include "RoofText.h"

// Spec 023: the roof without valid pins, and what the wiring check reads: the
// inputs, and EN's pull-down through MotorDriver (both PWM held low).

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

    RoofControl::Outcome RoofControl::notStarted() const
    {
        if (!pinsValid)
            return Outcome{false, "pinSetup", pinsReason, Roof::State::Unknown};
        return Outcome{false, "notStarted", "The roof controller didn't start: the motor stays off", Roof::State::Unknown};
    }

    MotorDriver::EnableProbe RoofControl::probeEnable(std::string &why)
    {
        if (lock == nullptr || !started.load())
        {
            why = notStarted().text;
            return MotorDriver::EnableProbe::NotRun;
        }
        RoofLocked held(lock);
        const Roof::Status &status = controller.status();
        const bool still = !RoofIo::roofMoving(status.state) && !closePending && !calibration.active() &&
                           controller.output().command == Driver::Command::Off;
        if (!still)
        {
            why = "The roof is busy";
            return MotorDriver::EnableProbe::NotRun;
        }
        return MotorDriver::probeEnable();
    }

    // Spec 023 review: stored analog values that fail their checks (saved before a
    // rule existed) are never driven with: the settings count as invalid.
    Roof::Settings RoofControl::motionSettings(const HardwareSettings &s)
    {
        Roof::Settings motion = Roof::settingsFromConfig(s.roof);
        std::string why;
        motion.analogUsable = analogUsable(s.analog, s.roof, &why);
        if (!motion.analogUsable)
            Logger::error(TAG, "Analog setup refused, every move is refused until it is fixed: %s", why.c_str());
        return motion;
    }

    RoofControl::HardwareSettings RoofControl::settingsOf(const Config &config)
    {
        HardwareSettings s;
        s.roof = config.roof;
        s.analog = config.analog;
        s.fittedSwitches = static_cast<uint8_t>(config.park.switches);
        s.switchesOnly = config.park.switchesOnly();
        return s;
    }

    void RoofControl::takePins(const Board::PinMap &inEffect, bool valid, const std::string &reason)
    {
        pins = inEffect;
        pinsValid = valid;
        pinsReason = reason;
    }

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
            in.currentAmps[side] = Board::scaledSenseAmps(
                mv,
                side == 0 ? settings.analog.currentR : settings.analog.currentL,
                side == 0 ? settings.analog.currentRTrim : settings.analog.currentLTrim,
                settings.roof.currentAmpsPerVolt);
        }
    }
} // namespace SQM
