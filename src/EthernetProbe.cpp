#include "EthernetProbe.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include "Mdio.h"

namespace SQM
{
    namespace EthernetProbe
    {
        namespace
        {
            constexpr uint32_t MDC_HALF_PERIOD_US = 5; // about 100 kHz
            constexpr uint32_t OSCILLATOR_START_MS = 10;
            constexpr size_t CLOCK_SAMPLES = 64;

            void mdioBit(bool level)
            {
                const auto mdio = static_cast<gpio_num_t>(Board::ETH_MDIO);
                // MDIO is only ever pulled low or released to its pull-up.
                gpio_set_direction(mdio, level ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT);
                esp_rom_delay_us(MDC_HALF_PERIOD_US);
                gpio_set_level(static_cast<gpio_num_t>(Board::ETH_MDC), 1);
                esp_rom_delay_us(MDC_HALF_PERIOD_US);
                gpio_set_level(static_cast<gpio_num_t>(Board::ETH_MDC), 0);
            }

            // After the falling edge (Linux mdio-bitbang): the PHY launched the
            // bit on the rising edge, so it is stable here.
            bool mdioSample()
            {
                const auto mdio = static_cast<gpio_num_t>(Board::ETH_MDIO);
                gpio_set_direction(mdio, GPIO_MODE_INPUT);
                esp_rom_delay_us(MDC_HALF_PERIOD_US);
                gpio_set_level(static_cast<gpio_num_t>(Board::ETH_MDC), 1);
                esp_rom_delay_us(MDC_HALF_PERIOD_US);
                gpio_set_level(static_cast<gpio_num_t>(Board::ETH_MDC), 0);
                esp_rom_delay_us(MDC_HALF_PERIOD_US);
                return gpio_get_level(mdio) != 0;
            }

            bool mdioRead(uint8_t reg, uint16_t &value)
            {
                bool frame[Board::Mdio::DRIVEN_BITS];
                Board::Mdio::readFrame(Board::ETH_PHY_ADDRESS, reg, frame);
                for (bool bit : frame)
                    mdioBit(bit);
                bool reply[Board::Mdio::REPLY_BITS];
                for (bool &bit : reply)
                    bit = mdioSample();
                mdioSample(); // one idle clock, as the PHY releases the line
                return Board::Mdio::decodeRead(reply, value);
            }

            void setPin(uint8_t pin, gpio_mode_t mode, bool pullUp, bool pullDown)
            {
                gpio_config_t io = {};
                io.pin_bit_mask = 1ULL << pin;
                io.mode = mode;
                io.pull_up_en = pullUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
                io.pull_down_en = pullDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
                io.intr_type = GPIO_INTR_DISABLE;
                gpio_config(&io);
            }

        } // namespace

        // Research R7: the WT32's oscillator on for about 15 ms, the clock
        // on GPIO0, the PHY's ID registers; then everything released.
        void run(Board::EthernetFacts &e)
        {
            e.mode = Board::EthernetFacts::Mode::Probed;
            setPin(Board::ETH_MDC, GPIO_MODE_OUTPUT, false, false);
            gpio_set_level(static_cast<gpio_num_t>(Board::ETH_MDC), 0);
            gpio_set_level(static_cast<gpio_num_t>(Board::ETH_MDIO), 0); // driven only as a low
            setPin(Board::ETH_MDIO, GPIO_MODE_INPUT, true, false);
            setPin(Board::ETH_CLOCK, GPIO_MODE_INPUT, false, false);
            gpio_set_level(static_cast<gpio_num_t>(Board::ETH_POWER), 1);
            setPin(Board::ETH_POWER, GPIO_MODE_OUTPUT, false, false);
            gpio_set_level(static_cast<gpio_num_t>(Board::ETH_POWER), 1);
            delay(OSCILLATOR_START_MS);
            bool samples[CLOCK_SAMPLES];
            for (bool &sample : samples)
                sample = gpio_get_level(static_cast<gpio_num_t>(Board::ETH_CLOCK)) != 0;
            e.clockSeen = Board::Mdio::clockSeen(samples, CLOCK_SAMPLES);
            e.idRead = mdioRead(Board::Mdio::REG_ID1, e.id1) && mdioRead(Board::Mdio::REG_ID2, e.id2);
            // The oscillator off and held off by the pull-down; MDC and MDIO back to inputs.
            gpio_set_level(static_cast<gpio_num_t>(Board::ETH_POWER), 0);
            setPin(Board::ETH_POWER, GPIO_MODE_INPUT, false, true);
            setPin(Board::ETH_MDC, GPIO_MODE_INPUT, false, false);
            setPin(Board::ETH_MDIO, GPIO_MODE_INPUT, false, false);
        }

        bool foundWt32(const Board::EthernetFacts &facts)
        {
            return facts.idRead && Board::Mdio::identify(facts.id1, facts.id2) == Board::Mdio::Phy::Lan87xx;
        }
    } // namespace EthernetProbe
} // namespace SQM
