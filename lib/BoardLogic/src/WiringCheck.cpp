#include "WiringCheck.h"

#include <cstdio>

#include "Mdio.h"

namespace SQM
{
    namespace Board
    {
        namespace
        {
            template <typename... Args> std::string format(const char *pattern, Args... args)
            {
                char buffer[200];
                std::snprintf(buffer, sizeof(buffer), pattern, args...);
                return buffer;
            }

            Check make(CheckId id, Level level, std::string text, float value = NAN, const char *unit = "")
            {
                Check check;
                check.id = id;
                check.level = level;
                check.value = value;
                check.unit = unit;
                check.text = std::move(text);
                return check;
            }

            Check enCheck(EnProbe probe)
            {
                switch (probe)
                {
                case EnProbe::Low:
                    return make(
                        CheckId::EnPulldown, Level::Pass, "EN reads low with the internal pull-up: its pull-down holds the driver off");
                case EnProbe::High:
                    return make(
                        CheckId::EnPulldown,
                        Level::Fail,
                        "EN reads high with the internal pull-up: its pull-down is missing or weaker than about 20 kOhm, so a reset or a "
                        "crash wouldn't hold the driver off");
                default:
                    return make(
                        CheckId::EnPulldown, Level::Skipped, "Not checked by the bench firmware, which keeps EN, RPWM and LPWM low");
                }
            }

            Check hallCheck(const WiringInputs &in)
            {
                const unsigned counts = in.hallCounts;
                const auto value = static_cast<float>(counts);
                if (counts <= in.hallRailLow || counts >= in.hallRailHigh)
                    return make(
                        CheckId::Hall,
                        Level::Fail,
                        format("%u counts: at a rail, so the Hall sensor or its divider is disconnected or shorted", counts),
                        value,
                        "counts");
                if (counts < in.hallMin || counts > in.hallMax)
                    return make(
                        CheckId::Hall,
                        Level::Warn,
                        format(
                            "%u counts: outside the thresholds' range (%u-%u); check the sensor's divider",
                            counts,
                            static_cast<unsigned>(in.hallMin),
                            static_cast<unsigned>(in.hallMax)),
                        value,
                        "counts");
                return make(CheckId::Hall, Level::Pass, format("%u counts: a plausible reading", counts), value, "counts");
            }

            Check supplyCheck(const WiringInputs &in)
            {
                const float volts = in.supplyVolts;
                const double shown = static_cast<double>(volts);
                if (!std::isfinite(volts))
                    return make(CheckId::Supply, Level::Fail, "The supply sense can't be read");
                if (volts < SUPPLY_ABSENT_VOLTS)
                    return make(
                        CheckId::Supply, Level::Fail, format("%.1f V: no supply, or its divider isn't connected", shown), volts, "V");
                if (volts > SUPPLY_MAX_VOLTS)
                    return make(
                        CheckId::Supply, Level::Warn, format("%.1f V: above 15 V, so check the divider (100k/22k)", shown), volts, "V");
                if (volts < in.supplyLimit)
                    return make(
                        CheckId::Supply,
                        Level::Warn,
                        format("%.1f V: below the %.1f V limit, so the roof won't move", shown, static_cast<double>(in.supplyLimit)),
                        volts,
                        "V");
                return make(CheckId::Supply, Level::Pass, format("%.1f V: a plausible supply", shown), volts, "V");
            }

            Check parkCheck(CheckId id, bool fitted, SwitchLevel level)
            {
                if (!fitted)
                    return make(id, Level::Skipped, "Not fitted (Settings, Roof, park switches)");
                switch (level)
                {
                case SwitchLevel::Open:
                    return make(id, Level::Pass, "Open: not parked (the input is pulled up, a closed switch reads as parked)");
                case SwitchLevel::Closed:
                    return make(id, Level::Pass, "Closed: parked (the switch pulls the input to ground)");
                default:
                    return make(id, Level::Warn, "Unreadable: the input changes too often, so check the switch and its wire");
                }
            }

            Check currentCheck(CheckId id, const WiringInputs &in, size_t side)
            {
                if (in.currentSense)
                {
                    const float amps = in.currentAmps[side];
                    const double shown = static_cast<double>(amps);
                    if (!std::isfinite(amps))
                        return make(id, Level::Warn, "The current sense can't be read");
                    if (amps < CURRENT_REST_AMPS)
                        return make(id, Level::Pass, format("%.1f A while stopped: near zero", shown), amps, "A");
                    return make(id, Level::Warn, format("%.1f A while stopped, so check the IS wire and its divider", shown), amps, "A");
                }
                const float mv = in.currentMillivolts[side];
                const double shownMv = static_cast<double>(mv);
                if (!std::isfinite(mv))
                    return make(id, Level::Skipped, "Not read");
                if (mv < CURRENT_REST_MILLIVOLTS)
                    return make(id, Level::Pass, format("%.0f mV while stopped: near zero (current sense is off)", shownMv), mv, "mV");
                return make(
                    id,
                    Level::Warn,
                    format("%.0f mV while stopped (current sense is off), so check the pin and its divider", shownMv),
                    mv,
                    "mV");
            }

            Check runningEthernet(const EthernetFacts &e)
            {
                const auto speed = static_cast<unsigned>(e.speedMbps);
                if (!e.started || !e.phyFound)
                    return make(CheckId::Ethernet, Level::Fail, "Ethernet didn't start: the LAN8720 didn't answer");
                if (!e.link)
                    return make(CheckId::Ethernet, Level::Warn, "The LAN8720 answered but there is no link, so check the cable");
                if (!e.hasAddress)
                    return make(
                        CheckId::Ethernet,
                        Level::Warn,
                        format("Link up at %u Mbit/s, no address yet", speed),
                        static_cast<float>(speed),
                        "Mbit/s");
                return make(
                    CheckId::Ethernet,
                    Level::Pass,
                    format("Link up at %u Mbit/s with an address", speed),
                    static_cast<float>(speed),
                    "Mbit/s");
            }

            Check probedEthernet(const EthernetFacts &e, bool &suggest)
            {
                const Mdio::Phy phy = e.idRead ? Mdio::identify(e.id1, e.id2) : Mdio::Phy::None;
                if (phy == Mdio::Phy::Lan87xx)
                {
                    suggest = true;
                    return make(
                        CheckId::Ethernet,
                        Level::Warn,
                        "A LAN8720 Ethernet PHY answered at address 1: this looks like a WT32-ETH01, so choose its preset");
                }
                if (phy == Mdio::Phy::Other)
                    return make(
                        CheckId::Ethernet,
                        Level::Warn,
                        format(
                            "An Ethernet PHY answered (ID %04X:%04X), not the WT32-ETH01's LAN8720",
                            static_cast<unsigned>(e.id1),
                            static_cast<unsigned>(e.id2)));
                if (e.clockSeen)
                    return make(CheckId::Ethernet, Level::Warn, "A clock on GPIO0 but no Ethernet PHY answered");
                return make(CheckId::Ethernet, Level::Pass, "No Ethernet PHY found, as expected on an ESP32 DevKit");
            }

            Check ethernetCheck(const EthernetFacts &e, bool &suggest)
            {
                switch (e.mode)
                {
                case EthernetFacts::Mode::Running:
                    return runningEthernet(e);
                case EthernetFacts::Mode::Probed:
                    return probedEthernet(e, suggest);
                case EthernetFacts::Mode::SkippedPin:
                    return make(
                        CheckId::Ethernet,
                        Level::Skipped,
                        format("Not probed: GPIO%u is a roof function", static_cast<unsigned>(e.usedPin)));
                default:
                    return make(CheckId::Ethernet, Level::Skipped, "Not probed: this chip's PSRAM uses GPIO16");
                }
            }
        } // namespace

        WiringResult evaluate(const WiringInputs &in)
        {
            WiringResult result;
            result.checks.push_back(enCheck(in.en));
            result.checks.push_back(hallCheck(in));
            result.checks.push_back(supplyCheck(in));
            result.checks.push_back(parkCheck(CheckId::ParkA, in.fittedSwitches >= 1, in.parkA));
            result.checks.push_back(parkCheck(CheckId::ParkB, in.fittedSwitches >= 2, in.parkB));
            result.checks.push_back(currentCheck(CheckId::CurrentR, in, 0));
            result.checks.push_back(currentCheck(CheckId::CurrentL, in, 1));
            result.checks.push_back(ethernetCheck(in.ethernet, result.suggestWt32));
            result.checks.push_back(make(
                CheckId::Motion,
                Level::Skipped,
                "Direction and travel need the roof to move: use Jog and teach the ends on the calibration page"));
            return result;
        }

        const char *checkIdName(CheckId id)
        {
            static const char *const NAMES[] = {
                "enPulldown", "hall", "supply", "parkA", "parkB", "currentR", "currentL", "ethernet", "motion"};
            const auto index = static_cast<size_t>(id);
            return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "";
        }

        const char *levelName(Level level)
        {
            static const char *const NAMES[] = {"pass", "warn", "fail", "skipped"};
            const auto index = static_cast<size_t>(level);
            return index < 4 ? NAMES[index] : "";
        }
    } // namespace Board
} // namespace SQM
