#include "MoveGuard.h"

#include <ArduinoJson.h>

#include "RoofText.h"

namespace SQM
{
    namespace RoofIo
    {
        MoveGuardSettings moveGuardSettingsOf(const Config &config)
        {
            MoveGuardSettings s;
            s.wait.limitMs = config.park.timeoutSeconds * 1000u;
            s.wait.gapMs = config.autoClose.parkRequestSeconds * 1000u;
            s.wait.holdMs = config.autoClose.parkHoldSeconds * 1000u;
            s.ninaSet = !config.park.ninaUrl.empty();
            s.requestPark = config.park.requestPark;
            return s;
        }

        Admission MoveGuard::admit(const Request &request, const CommandCheck &check, const CommandCheck &parkAside, uint32_t nowMs)
        {
            const Roof::Command command = request.command;
            const Roof::Origin origin = request.origin;
            const bool parkOnlyNow = !check.accepted && check.reason == gateRefusalName(GateRefusal::MountNotParked) && parkAside.accepted;
            if (parkWait.waiting() && parkWait.command() == command && parkOnlyNow && origin != Roof::Origin::Automatic)
                return Admission{Admit::Wait, "", parkWaitText(command)}; // joins the wait under way
            parkWait.cancel("replaced by a new command");
            if (check.accepted)
                return Admission{Admit::Run, "", ""};
            if (!parkOnlyNow || origin == Roof::Origin::Automatic)
                return Admission{Admit::Refuse, check.reason, check.text};
            const std::string impossible = parkWaitImpossible(settings.ninaSet, settings.requestPark);
            if (!impossible.empty())
                return Admission{Admit::Refuse, check.reason, check.text + ": " + impossible};
            parkWait.start(command, origin, request.overrides, nowMs, settings.wait);
            return Admission{Admit::Wait, "", parkWaitText(command)};
        }

        void MoveGuard::ran(Roof::Command command, Roof::Origin origin, uint8_t overrides, const GateInputs &gates)
        {
            const uint8_t allowed = allowedOverrides(command, origin, overrides);
            if (command == Roof::Command::Open)
            {
                openBits =
                    static_cast<uint8_t>(allowed & overridableNow(Roof::Command::Open, gates) & (OverrideWeather | OverrideDaylight));
                openPark = (allowed & OverridePark) != 0; // spec 039
                closeBits = 0;
            }
            else if (command == Roof::Command::Close)
            {
                openBits = 0;
                openPark = false;
                closeBits = static_cast<uint8_t>(allowed & (OverridePark | OverrideNinaLost)); // spec 030: N.I.N.A. lost
            }
        }

        void MoveGuard::closeWins()
        {
            if (parkWait.waiting() && parkWait.command() == Roof::Command::Open)
                parkWait.cancel("the automatic close takes over");
        }

        void MoveGuard::stopped()
        {
            parkWait.cancel("stopped");
        }

        GuardStep MoveGuard::step(
            const Roof::Status &status, bool closePending, const GateInputs &gates, const CommandCheck &waitCheck, uint32_t nowMs)
        {
            GuardStep out;
            openBits = status.state == Roof::State::Closed ? 0 : overrideStillApplies(openBits, gates);
            if (closeBits != 0 && status.state != Roof::State::Closing && !closePending)
                closeBits = 0;
            // Spec 039: for that one open only; it ends with the open (stopped, reached, reversed).
            if (openPark && status.state != Roof::State::Opening)
                openPark = false;
            if (!parkWait.waiting())
                return out;
            if (!waitCheck.accepted)
            {
                parkWait.refuse(waitCheck.text);
                out.refused = true;
                return out;
            }
            const ParkWaitStep step = parkWait.step(parkHolds(gates), nowMs);
            out.requestPark = step.requestPark;
            out.run = step.end == ParkWaitEnd::Ran;
            out.gaveUp = step.end == ParkWaitEnd::GaveUp;
            return out;
        }

        bool parseOverrides(const std::string &body, uint8_t &bits, std::string &error)
        {
            bits = 0;
            if (body.find_first_not_of(" \t\r\n") == std::string::npos)
                return true;
            StaticJsonDocument<256> doc;
            const bool read = !deserializeJson(doc, body) && doc.is<JsonObject>();
            JsonVariantConst list = doc["override"];
            if (!read || (!list.isNull() && !list.is<JsonArrayConst>()))
            {
                error = "The body must be JSON: {\"override\": [\"weatherNotSafe\", \"daylight\", \"mountNotParked\"]}";
                return false;
            }
            for (JsonVariantConst name : list.as<JsonArrayConst>())
            {
                const uint8_t bit = name.is<const char *>() ? overrideBit(name.as<const char *>()) : 0;
                if (bit == 0)
                {
                    error = "Only mountNotParked (Open or Close), weatherNotSafe and daylight (Open) can be overridden";
                    return false;
                }
                bits = static_cast<uint8_t>(bits | bit);
            }
            return true;
        }

        std::string parkWaitText(Roof::Command command)
        {
            return std::string("Waiting for the mount to park: N.I.N.A. was asked; the ") + commandWord(command) +
                   " runs once the park holds";
        }

        std::string parkOverrideText(Roof::Command command, const GateInputs &gates)
        {
            const std::string why =
                parkHolds(gates) ? std::string("the park was confirmed") : describeGate(GateRefusal::MountNotParked, gates);
            return std::string(commandWord(command)) + " with the park check set aside by a person (" + why +
                   "): the roof passes over the telescope, watch it";
        }

        const char *overrideName(uint8_t bit)
        {
            switch (bit)
            {
            case OverridePark:
                return "mountNotParked";
            case OverrideWeather:
                return "weatherNotSafe";
            case OverrideDaylight:
                return "daylight";
            default:
                return "";
            }
        }
    } // namespace RoofIo
} // namespace SQM
