#pragma once

// The analog front end as settings (spec 023, owner requirement "Advanced"):
// each analog input's divider and an optional trim, with presets per board,
// and the warnings when a divider could put more than 3.3 V on an ADC pin.
// The Hall thresholds are raw ADC counts, so a Hall divider changes nothing
// the motion logic compares; it is checked for the pin's voltage only.

#include <cstdint>
#include <string>
#include <vector>

namespace SQM
{
    namespace Board
    {
        enum class AnalogPreset : uint8_t
        {
            DevkitBench = 0,    // the DevKit bench wiring (the values before this setting)
            Wt32CarrierV11 = 1, // the WT32 carrier v1.1: its supply sense sits after a reverse-polarity diode
            Custom = 2,
        };

        struct Divider
        {
            uint32_t topOhms = 10000;
            uint32_t bottomOhms = 20000;
            // Input volts per pin volt; 0 when the divider can't be used.
            float ratio() const;
        };

        // value = (pin volts x divider ratio) x gain + offset, in the input's
        // own unit (volts for the supply, amps for current sense).
        struct Trim
        {
            float gain = 1.0f;
            float offset = 0.0f;
        };

        struct AnalogConfig
        {
            AnalogPreset preset = AnalogPreset::DevkitBench;
            Divider supply{100000, 22000};
            Trim supplyTrim; // offset in volts (e.g. a diode drop, +0.3 V)
            Divider hall{10000, 20000};
            Divider currentR{10000, 20000};
            Trim currentRTrim; // offset in amps
            Divider currentL{10000, 20000};
            Trim currentLTrim;

            bool operator==(const AnalogConfig &other) const;
            bool operator!=(const AnalogConfig &other) const { return !(*this == other); }
        };

        AnalogConfig analogPreset(AnalogPreset preset);
        constexpr bool isNamed(AnalogPreset preset)
        {
            return preset != AnalogPreset::Custom;
        }
        const char *analogPresetName(AnalogPreset preset);
        bool parseAnalogPreset(const std::string &name, AnalogPreset &out);

        // The ranges a value must be in (errors).
        constexpr uint32_t MIN_RESISTOR_OHMS = 100;
        constexpr uint32_t MAX_RESISTOR_OHMS = 10000000;
        constexpr float MIN_GAIN = 0.5f;
        constexpr float MAX_GAIN = 2.0f;
        constexpr float MAX_SUPPLY_OFFSET_VOLTS = 2.0f;
        constexpr float MAX_CURRENT_OFFSET_AMPS = 5.0f;
        // The most each sensor puts out (warnings above 3.3 V at the pin): a
        // 12 V system's supply at its highest, the SS49E on 5 V, the BTS7960's IS.
        constexpr float SUPPLY_MAX_VOLTS_IN = 15.0f;
        constexpr float HALL_MAX_VOLTS_IN = 4.2f;
        constexpr float IS_MAX_VOLTS_IN = 5.0f;
        constexpr float ADC_SAFE_VOLTS = 3.3f;

        struct AnalogIssue
        {
            std::string input; // "supply", "hall", "currentR", "currentL"
            bool error = true;
            std::string text;
        };
        std::vector<AnalogIssue> checkAnalog(const AnalogConfig &analog);

        // The scaled readings. NaN in, NaN out.
        float scaledSupplyVolts(float pinMillivolts, const AnalogConfig &analog);
        float scaledSenseAmps(float pinMillivolts, const Divider &divider, const Trim &trim, float ampsPerVolt);

        // The supply gain that makes `nowVolts` (scaled with `current`) read as
        // `measuredVolts`, keeping the offset; 0 when it can't be worked out.
        float calibratedSupplyGain(const Trim &current, float nowVolts, float measuredVolts);
    } // namespace Board
} // namespace SQM
