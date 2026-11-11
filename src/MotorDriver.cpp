#include "MotorDriver.h"

#include <Arduino.h>
#include <atomic>
#include <driver/gpio.h>

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

            void setEnable(bool on)
            {
                // Read before the clock: a beat stamped in between counts as fresh.
                const uint32_t beat = beatMs.load();
                const bool alive = RoofIo::watchdogAlive(millis(), beat);
                portENTER_CRITICAL(&enableLock);
                const bool level = on && !tripSet && armed && alive;
                gpio_set_level(static_cast<gpio_num_t>(Pins::ENABLE), level ? 1 : 0);
                enOn.store(level);
                portEXIT_CRITICAL(&enableLock);
            }

            void setPwm(uint8_t pin, uint8_t duty)
            {
                if (pwmReady)
                    ledcWrite(pin, duty);
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
            const bool right = ledcAttach(Pins::RPWM, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
            const bool left = ledcAttach(Pins::LPWM, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
            pwmReady = right && left;
            setPwm(Pins::RPWM, 0);
            setPwm(Pins::LPWM, 0);
            return pwmReady;
        }

        void apply(const Driver::Outputs &outputs)
        {
            // Without PWM nothing can be driven: hold EN low.
            if (!outputs.enable || !pwmReady)
            {
                setEnable(false);
                setPwm(Pins::RPWM, 0);
                setPwm(Pins::LPWM, 0);
                return;
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
            setEnable(true);
        }

        void trip()
        {
            portENTER_CRITICAL(&enableLock);
            tripSet = true;
            tripMark = TRIP_MARK;
            gpio_set_level(static_cast<gpio_num_t>(Pins::ENABLE), 0);
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
    } // namespace MotorDriver
} // namespace SQM
