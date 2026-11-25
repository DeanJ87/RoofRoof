#include "MoveGuard.h"

#include <ArduinoJson.h>

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

        void MoveGuard::ran(Roof::Command command, uint8_t overrides, const GateInputs &gates)
        {
            if (command == Roof::Command::Open)
            {
                openBits = static_cast<uint8_t>(overrides & overridableNow(Roof::Command::Open, gates));
                closeAside = false;
            }
            else if (command == Roof::Command::Close)
            {
                openBits = 0;
                closeAside = (overrides & OverridePark) != 0;
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
            if (closeAside && status.state != Roof::State::Closing && !closePending)
                closeAside = false;
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
                error = "The body must be JSON: {\"override\": [\"weatherNotSafe\", \"daylight\"]} or {\"override\": [\"mountNotParked\"]}";
                return false;
            }
            for (JsonVariantConst name : list.as<JsonArrayConst>())
            {
                const uint8_t bit = name.is<const char *>() ? overrideBit(name.as<const char *>()) : 0;
                if (bit == 0)
                {
                    error = "Only mountNotParked (Close), weatherNotSafe and daylight (Open) can be overridden";
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
