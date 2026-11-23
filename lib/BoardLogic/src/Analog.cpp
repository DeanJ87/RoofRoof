#include "Analog.h"

#include <cmath>
#include <cstdio>

namespace SQM
{
    namespace Board
    {
        namespace
        {
            constexpr const char *ANALOG_PRESET_NAMES[] = {"devkit-bench", "wt32-carrier-v1.1", "custom"};

            template <typename... Args> std::string format(const char *pattern, Args... args)
            {
                char buffer[200];
                std::snprintf(buffer, sizeof(buffer), pattern, args...);
                return buffer;
            }

            bool sameDivider(const Divider &a, const Divider &b)
            {
                return a.topOhms == b.topOhms && a.bottomOhms == b.bottomOhms;
            }

            bool sameTrim(const Trim &a, const Trim &b)
            {
                return a.gain == b.gain && a.offset == b.offset;
            }

            bool resistorOk(uint32_t ohms)
            {
                return ohms >= MIN_RESISTOR_OHMS && ohms <= MAX_RESISTOR_OHMS;
            }

            void checkDivider(std::vector<AnalogIssue> &out, const char *input, const char *label, const Divider &d, float sensorMax)
            {
                if (!resistorOk(d.topOhms) || !resistorOk(d.bottomOhms))
                {
                    out.push_back({input, true, format("%s divider: each resistor must be between 100 Ohm and 10 MOhm", label)});
                    return;
                }
                const float atPin = sensorMax / d.ratio();
                if (atPin > ADC_SAFE_VOLTS)
                    out.push_back(
                        {input,
                         false,
                         format(
                             "%s divider: at its highest (%.1f V) the input would put %.2f V on the pin, over 3.3 V",
                             label,
                             static_cast<double>(sensorMax),
                             static_cast<double>(atPin))});
            }

            struct TrimLimits
            {
                const char *input;
                const char *label;
                float maxOffset;
                const char *unit;
            };

            void checkTrim(std::vector<AnalogIssue> &out, const TrimLimits &limits, const Trim &t)
            {
                const char *label = limits.label;
                const double maxOffset = static_cast<double>(limits.maxOffset);
                if (!(t.gain >= MIN_GAIN && t.gain <= MAX_GAIN))
                    out.push_back({limits.input, true, format("%s trim: the gain must be between 0.5 and 2", label)});
                if (!(std::fabs(t.offset) <= limits.maxOffset))
                    out.push_back(
                        {limits.input, true, format("%s trim: the offset must be within %.0f %s", label, maxOffset, limits.unit)});
            }
        } // namespace

        float Divider::ratio() const
        {
            if (bottomOhms == 0)
                return 0.0f;
            return static_cast<float>(static_cast<double>(topOhms + bottomOhms) / static_cast<double>(bottomOhms));
        }

        bool AnalogConfig::operator==(const AnalogConfig &o) const
        {
            return preset == o.preset && sameDivider(supply, o.supply) && sameTrim(supplyTrim, o.supplyTrim) && sameDivider(hall, o.hall) &&
                   sameDivider(currentR, o.currentR) && sameTrim(currentRTrim, o.currentRTrim) && sameDivider(currentL, o.currentL) &&
                   sameTrim(currentLTrim, o.currentLTrim);
        }

        AnalogConfig analogPreset(AnalogPreset preset)
        {
            AnalogConfig a; // the DevKit bench wiring
            a.preset = preset;
            if (preset == AnalogPreset::Wt32CarrierV11)
                a.supplyTrim.offset = 0.3f; // the reverse-polarity diode before the supply divider
            return a;
        }

        const char *analogPresetName(AnalogPreset preset)
        {
            const auto index = static_cast<size_t>(preset);
            return index < 3 ? ANALOG_PRESET_NAMES[index] : "custom";
        }

        bool parseAnalogPreset(const std::string &name, AnalogPreset &out)
        {
            for (size_t i = 0; i < 3; ++i)
                if (name == ANALOG_PRESET_NAMES[i])
                {
                    out = static_cast<AnalogPreset>(i);
                    return true;
                }
            return false;
        }

        std::vector<AnalogIssue> checkAnalog(const AnalogConfig &a)
        {
            std::vector<AnalogIssue> out;
            checkDivider(out, "supply", "Supply sense", a.supply, SUPPLY_MAX_VOLTS_IN);
            checkDivider(out, "hall", "Hall sensor", a.hall, HALL_MAX_VOLTS_IN);
            checkDivider(out, "currentR", "R_IS", a.currentR, IS_MAX_VOLTS_IN);
            checkDivider(out, "currentL", "L_IS", a.currentL, IS_MAX_VOLTS_IN);
            checkTrim(out, {"supply", "Supply sense", MAX_SUPPLY_OFFSET_VOLTS, "V"}, a.supplyTrim);
            checkTrim(out, {"currentR", "R_IS", MAX_CURRENT_OFFSET_AMPS, "A"}, a.currentRTrim);
            checkTrim(out, {"currentL", "L_IS", MAX_CURRENT_OFFSET_AMPS, "A"}, a.currentLTrim);
            if (isNamed(a.preset) && a != analogPreset(a.preset))
                out.push_back({"", true, "These values belong to a named preset; choose Custom to change one"});
            return out;
        }

        float scaledSupplyVolts(float pinMillivolts, const AnalogConfig &a)
        {
            return pinMillivolts / 1000.0f * a.supply.ratio() * a.supplyTrim.gain + a.supplyTrim.offset;
        }

        float scaledSenseAmps(float pinMillivolts, const Divider &divider, const Trim &trim, float ampsPerVolt)
        {
            return pinMillivolts / 1000.0f * divider.ratio() * ampsPerVolt * trim.gain + trim.offset;
        }

        float calibratedSupplyGain(const Trim &current, float nowVolts, float measuredVolts)
        {
            const float untrimmed = (nowVolts - current.offset) / current.gain;
            if (!std::isfinite(untrimmed) || untrimmed <= 0.1f || !std::isfinite(measuredVolts) || measuredVolts <= current.offset)
                return 0.0f;
            return (measuredVolts - current.offset) / untrimmed;
        }
    } // namespace Board
} // namespace SQM
