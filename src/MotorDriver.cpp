#include "MotorDriver.h"

#include <Arduino.h>
#include <atomic>
#include <driver/gpio.h>

#include "BenchMode.h"
#include "MotionGuards.h"
#include "RoofPins.h"

namespace SQM
{
    namespace MotorDriver
    {
        namespace
        {
            // Guards EN against the watchdog: setting EN high and tripping are
            // each one critical section, so a step racing a trip can't switch
            // the driver back on.
            portMUX_TYPE enableLock = portMUX_INITIALIZER_UNLOCKED;
            bool tripSet = false;          // guarded by enableLock
            bool armed = false;            // guarded by enableLock: the control task and watchdog run
            std::atomic<bool> enOn{false}; // what EN was last set to
            bool pwmReady = false;

            // Survives a reset that isn't a power cut: set by every trip, so a
            // trip whose fault never reached NVS (the task hung and the task
            // watchdog restarted the device) is still latched after the restart.
            constexpr uint32_t TRIP_MARK = 0x52574454; // "RWDT"
            RTC_NOINIT_ATTR uint32_t tripMark;

            // The watchdog's heartbeat: EN can't go high unless it is fresh.
            std::atomic<uint32_t> beatMs{0};

            // The duties last asked for (spec 011: what the simulated roof
            // follows in a bench build; the pins never see them there).
            std::atomic<uint8_t> dutyR{0};
            std::atomic<uint8_t> dutyL{0};

            // The one place a level reaches EN (spec 011 FR-021): always low in
            // a bench build.
            void writeEnable(bool level)
            {
                Driver::Outputs logical;
                logical.enable = level;
                const Driver::Outputs pins = Driver::physicalOutputs(logical, Bench::BUILD);
                gpio_set_level(static_cast<gpio_num_t>(Pins::ENABLE), pins.enable ? 1 : 0);
            }

            ApplyResult setEnable(bool on)
            {
                // Read before the clock: a beat stamped in between counts as fresh.
                const uint32_t beat = beatMs.load();
                const bool alive = RoofIo::watchdogAlive(millis(), beat);
                portENTER_CRITICAL(&enableLock);
                ApplyResult result = ApplyResult::Applied;
                if (on && tripSet)
                    result = ApplyResult::HeldTripped;
                else if (on && !armed)
                    result = ApplyResult::HeldUnarmed;
                else if (on && !alive)
                    result = ApplyResult::HeldWatchdog;
                const bool level = on && result == ApplyResult::Applied;
                writeEnable(level);
                enOn.store(level);
                portEXIT_CRITICAL(&enableLock);
                return result;
            }

            // The one place a duty reaches RPWM or LPWM: never in a bench build,
            // where PWM isn't even attached (the pins stay plain outputs, low).
            void setPwm(uint8_t pin, uint8_t duty)
            {
                (pin == Pins::RPWM ? dutyR : dutyL).store(duty);
                Driver::Outputs logical;
                (pin == Pins::RPWM ? logical.rpwm : logical.lpwm) = duty;
                const Driver::Outputs pins = Driver::physicalOutputs(logical, Bench::BUILD);
                if (pwmReady && !Bench::BUILD)
                    ledcWrite(pin, pin == Pins::RPWM ? pins.rpwm : pins.lpwm);
            }
        } // namespace

        void holdOff()
        {
            const Driver::Outputs off = Driver::outputsFor(Driver::Command::Off, 0);
            // EN first: with it low the bridge ignores the PWM inputs. Each
            // level goes into the output register before the pin becomes an
            // output, so it never drives high for an instant (gpio_set_level
            // works on a pin not yet configured; Arduino's digitalWrite doesn't).
            gpio_set_level(static_cast<gpio_num_t>(Pins::ENABLE), off.enable ? 1 : 0);
            pinMode(Pins::ENABLE, OUTPUT);
            gpio_set_level(static_cast<gpio_num_t>(Pins::RPWM), off.rpwm > 0 ? 1 : 0);
            pinMode(Pins::RPWM, OUTPUT);
            gpio_set_level(static_cast<gpio_num_t>(Pins::LPWM), off.lpwm > 0 ? 1 : 0);
            pinMode(Pins::LPWM, OUTPUT);
        }

        bool begin()
        {
            setEnable(false);
            if (Bench::BUILD)
            {
                // Spec 011: no PWM on the pins at all. The logical driver
                // works as on the roof so the simulated roof can follow it.
                pwmReady = true;
                setPwm(Pins::RPWM, 0);
                setPwm(Pins::LPWM, 0);
                return true;
            }
            const bool right = ledcAttach(Pins::RPWM, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
            const bool left = ledcAttach(Pins::LPWM, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
            pwmReady = right && left;
            setPwm(Pins::RPWM, 0);
            setPwm(Pins::LPWM, 0);
            return pwmReady;
        }

        ApplyResult apply(const Driver::Outputs &outputs)
        {
            // Without PWM nothing can be driven: hold EN low.
            if (!outputs.enable || !pwmReady)
            {
                setEnable(false);
                setPwm(Pins::RPWM, 0);
                setPwm(Pins::LPWM, 0);
                return outputs.enable ? ApplyResult::HeldUnarmed : ApplyResult::Applied;
            }
            // lib/DriverLogic never sets both; the zero goes first anyway.
            if (outputs.rpwm > 0)
            {
                setPwm(Pins::LPWM, 0);
                setPwm(Pins::RPWM, outputs.rpwm);
            }
            else
            {
                setPwm(Pins::RPWM, 0);
                setPwm(Pins::LPWM, outputs.lpwm);
            }
            return setEnable(true);
        }

        void trip()
        {
            portENTER_CRITICAL(&enableLock);
            tripSet = true;
            tripMark = TRIP_MARK;
            writeEnable(false);
            enOn.store(false);
            portEXIT_CRITICAL(&enableLock);
        }

        bool tripped()
        {
            portENTER_CRITICAL(&enableLock);
            const bool set = tripSet;
            portEXIT_CRITICAL(&enableLock);
            return set;
        }

        void clearTrip()
        {
            portENTER_CRITICAL(&enableLock);
            tripSet = false;
            portEXIT_CRITICAL(&enableLock);
        }

        void watchdogBeat()
        {
            beatMs.store(millis());
        }

        uint32_t watchdogBeatMs()
        {
            return beatMs.load();
        }

        void arm()
        {
            portENTER_CRITICAL(&enableLock);
            armed = true;
            portEXIT_CRITICAL(&enableLock);
        }

        bool tripMarked()
        {
            return tripMark == TRIP_MARK;
        }

        void clearTripMark()
        {
            tripMark = 0;
        }

        bool enabled()
        {
            return enOn.load();
        }

        Driver::Outputs logicalOutputs()
        {
            Driver::Outputs out;
            out.enable = enOn.load();
            out.rpwm = dutyR.load();
            out.lpwm = dutyL.load();
            return out;
        }
    } // namespace MotorDriver
} // namespace SQM
