#include "MotorDriver.h"

#include <Arduino.h>
#include <atomic>
#include <driver/gpio.h>

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
            std::atomic<bool> enOn{false}; // what EN was last set to
            bool pwmReady = false;

            void setEnable(bool on)
            {
                portENTER_CRITICAL(&enableLock);
                const bool level = on && !tripSet;
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
            // level is set before the pin becomes an output, so it never
            // drives high for an instant.
            digitalWrite(Pins::ENABLE, off.enable ? HIGH : LOW);
            pinMode(Pins::ENABLE, OUTPUT);
            digitalWrite(Pins::RPWM, off.rpwm > 0 ? HIGH : LOW);
            pinMode(Pins::RPWM, OUTPUT);
            digitalWrite(Pins::LPWM, off.lpwm > 0 ? HIGH : LOW);
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

        bool enabled()
        {
            return enOn.load();
        }
    } // namespace MotorDriver
} // namespace SQM
