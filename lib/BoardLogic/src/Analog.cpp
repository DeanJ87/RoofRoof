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

            bool nearTrim(const Trim &a, const Trim &b)
            {
                return std::fabs(a.gain - b.gain) <= PRESET_TOLERANCE && std::fabs(a.offset - b.offset) <= PRESET_TOLERANCE;
            }

            // Whether a named preset's values are kept, within PRESET_TOLERANCE.
            bool matchesPreset(const AnalogConfig &a)
            {
                const AnalogConfig p = analogPreset(a.preset);
                return sameDivider(a.supply, p.supply) && sameDivider(a.hall, p.hall) && sameDivider(a.currentR, p.currentR) &&
                       sameDivider(a.currentL, p.currentL) && nearTrim(a.supplyTrim, p.supplyTrim) &&
                       nearTrim(a.currentRTrim, p.currentRTrim) && nearTrim(a.currentLTrim, p.currentLTrim);
            }

            bool resistorOk(uint32_t ohms)
            {
                return ohms >= MIN_RESISTOR_OHMS && ohms <= MAX_RESISTOR_OHMS;
            }

            bool dividerOk(const Divider &d)
            {
                return resistorOk(d.topOhms) && resistorOk(d.bottomOhms);
            }

            void checkDivider(std::vector<AnalogIssue> &out, const char *input, const char *label, const Divider &d, float sensorMax)
            {
                if (!dividerOk(d))
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

            bool trimOk(const Trim &t, float maxOffset)
            {
                return t.gain >= MIN_GAIN && t.gain <= MAX_GAIN && std::fabs(t.offset) <= maxOffset;
            }

            void checkTrim(std::vector<AnalogIssue> &out, const TrimLimits &limits, const Trim &t)
            {
                const char *label = limits.label;
                const double maxOffset = static_cast<double>(limits.maxOffset);
                if (!(t.gain >= MIN_GAIN && t.gain <= MAX_GAIN))
                    out.push_back({limits.input, true, format("%s trim: the gain must be between 0.8 and 1.25", label)});
                if (!(std::fabs(t.offset) <= limits.maxOffset))
                    out.push_back(
                        {limits.input, true, format("%s trim: the offset must be within %.1f %s", label, maxOffset, limits.unit)});
            }

            // With current sense on, over-current must be able to trip: the highest
            // reading must reach the limit. While driving, the reading is scaled back
            // by the duty, so the offset counts 100 / slow duty times over: a broken
            // sense wire (0 V, read as the offset) must stay below the broken-wire
            // floor, and a negative offset may neither read as unreadable nor move
            // the over-current point by more than MAX_CURRENT_OFFSET_AMPS.
            // The offset as a driven reading shows it: scaled back by 255 / the slow
            // duty as the roof drives it (Roof::toDuty's rounding), the lowest duty a move uses.
            float drivenOffset(float offset, float slowDutyPercent)
            {
                const float fraction = std::fmin(slowDutyPercent, 100.0f) / 100.0f; // as dutyFraction, then toDuty
                const long duty = fraction >= 1.0f ? 255 : std::lround(static_cast<double>(fraction) * 255.0);
                return duty > 0 ? offset * 255.0f / static_cast<float>(duty) : NAN;
            }

            void checkSenseGates(
                std::vector<AnalogIssue> &out, const TrimLimits &limits, const Divider &d, const Trim &t, const AnalogGates &g)
            {
                if (!g.currentSense || !dividerOk(d) || !trimOk(t, MAX_CURRENT_OFFSET_AMPS))
                    return;
                const float driven = drivenOffset(t.offset, g.slowDutyPercent);
                // The reach with the worse of the offset at full duty and at the slow duty.
                const float highest = highestSenseAmps(d, t, g.ampsPerVolt) - t.offset + std::fmin(t.offset, driven);
                if (!(highest >= g.maxCurrentAmps))
                    out.push_back(
                        {limits.input,
                         true,
                         format(
                             "%s: it can read at most %.1f A, below the current limit (%.1f A), so over-current could never trip",
                             limits.label,
                             static_cast<double>(highest),
                             static_cast<double>(g.maxCurrentAmps))});
                if (g.minRunningAmps > 0.0f && !(driven < g.minRunningAmps))
                    out.push_back(
                        {limits.input,
                         true,
                         format(
                             "%s trim: at the slow duty the offset reads %.2f A, not below the minimum running current (%.1f A), so a "
                             "broken sense wire would pass",
                             limits.label,
                             static_cast<double>(driven),
                             static_cast<double>(g.minRunningAmps))});
                if (!(driven >= -MAX_CURRENT_OFFSET_AMPS))
                    out.push_back(
                        {limits.input,
                         true,
                         format(
                             "%s trim: at the slow duty the offset reads %.2f A, below -0.5 A: a running motor could read as unreadable",
                             limits.label,
                             static_cast<double>(driven))});
            }

            // The supply trim may raise a reading at the low-supply limit by at most
            // MAX_SUPPLY_TRIM_RAISE_VOLTS, so it can't hide a real under-voltage.
            void checkSupplyGate(std::vector<AnalogIssue> &out, const Trim &t, const AnalogGates &g)
            {
                if (!trimOk(t, MAX_SUPPLY_OFFSET_VOLTS))
                    return;
                const float raise = (t.gain - 1.0f) * g.minSupplyVolts + t.offset;
                if (raise > MAX_SUPPLY_TRIM_RAISE_VOLTS)
                    out.push_back(
                        {"supply",
                         true,
                         format(
                             "Supply sense trim: at the minimum supply (%.1f V) it reads %.2f V high, over 1 V, which could hide a low "
                             "supply",
                             static_cast<double>(g.minSupplyVolts),
                             static_cast<double>(raise))});
            }
        } // namespace

        float Divider::ratio() const
        {
            if (bottomOhms == 0)
                return 0.0f;
            return static_cast<float>((static_cast<double>(topOhms) + static_cast<double>(bottomOhms)) / static_cast<double>(bottomOhms));
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

        std::vector<AnalogIssue> checkAnalog(const AnalogConfig &a, const AnalogGates &gates)
        {
            std::vector<AnalogIssue> out;
            checkDivider(out, "supply", "Supply sense", a.supply, SUPPLY_MAX_VOLTS_IN);
            checkDivider(out, "hall", "Hall sensor", a.hall, HALL_MAX_VOLTS_IN);
            checkDivider(out, "currentR", "R_IS", a.currentR, IS_MAX_VOLTS_IN);
            checkDivider(out, "currentL", "L_IS", a.currentL, IS_MAX_VOLTS_IN);
            checkTrim(out, {"supply", "Supply sense", MAX_SUPPLY_OFFSET_VOLTS, "V"}, a.supplyTrim);
            const TrimLimits right{"currentR", "R_IS", MAX_CURRENT_OFFSET_AMPS, "A"};
            const TrimLimits left{"currentL", "L_IS", MAX_CURRENT_OFFSET_AMPS, "A"};
            checkTrim(out, right, a.currentRTrim);
            checkTrim(out, left, a.currentLTrim);
            if (isNamed(a.preset) && !matchesPreset(a))
                out.push_back({"", true, "These values belong to a named preset; choose Custom to change one"});
            checkSupplyGate(out, a.supplyTrim, gates);
            checkSenseGates(out, right, a.currentR, a.currentRTrim, gates);
            checkSenseGates(out, left, a.currentL, a.currentLTrim, gates);
            return out;
        }

        float scaledSupplyVolts(float pinMillivolts, const AnalogConfig &a)
        {
            const float ratio = a.supply.ratio();
            if (!(ratio > 0.0f))
                return NAN;
            return pinMillivolts / 1000.0f * ratio * a.supplyTrim.gain + a.supplyTrim.offset;
        }

        float scaledSenseAmps(float pinMillivolts, const Divider &divider, const Trim &trim, float ampsPerVolt)
        {
            const float ratio = divider.ratio();
            if (!(ratio > 0.0f))
                return NAN;
            return pinMillivolts / 1000.0f * ratio * ampsPerVolt * trim.gain + trim.offset;
        }

        float highestSenseAmps(const Divider &divider, const Trim &trim, float ampsPerVolt)
        {
            const float ratio = divider.ratio();
            if (!(ratio > 0.0f))
                return NAN;
            const float pinVolts = std::fmin(ADC_FULL_SCALE_VOLTS, IS_MAX_VOLTS_IN / ratio);
            return scaledSenseAmps(pinVolts * 1000.0f, divider, trim, ampsPerVolt);
        }

        float calibratedSupplyGain(const AnalogConfig &saved, const AnalogConfig &edited, float nowVolts, float measuredVolts)
        {
            // Back to the pin's volts with the setup that scaled the reading.
            const float pinVolts = (nowVolts - saved.supplyTrim.offset) / saved.supplyTrim.gain / saved.supply.ratio();
            const float untrimmed = pinVolts * edited.supply.ratio();
            const float offset = edited.supplyTrim.offset;
            if (!std::isfinite(untrimmed) || untrimmed <= 0.1f || !std::isfinite(measuredVolts) || measuredVolts <= offset)
                return 0.0f;
            return (measuredVolts - offset) / untrimmed;
        }
    } // namespace Board
} // namespace SQM
