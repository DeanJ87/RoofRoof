#include "RoofTypes.h"

namespace SQM
{
    namespace Roof
    {
        namespace
        {
            // Name of `value` in `names`, or "unknown" for a value out of range (a bad
            // cast never indexes past the table).
            template <typename E, unsigned N> const char *nameIn(E value, const char *const (&names)[N])
            {
                const unsigned index = static_cast<unsigned>(value);
                return index < N ? names[index] : "unknown";
            }
        } // namespace

        const char *stateName(State state)
        {
            static const char *const NAMES[] = {"unknown", "closed", "open", "opening", "closing", "stopped", "error"};
            return nameIn(state, NAMES);
        }

        const char *commandName(Command command)
        {
            static const char *const NAMES[] = {"open", "close", "stop", "abort"};
            return nameIn(command, NAMES);
        }

        const char *endName(End end)
        {
            static const char *const NAMES[] = {"closed", "open"};
            return nameIn(end, NAMES);
        }

        const char *faultName(FaultKind kind)
        {
            static const char *const NAMES[] = {
                "none",
                "timeout",
                "didNotLeave",
                "wrongMagnet",
                "hallRail",
                "overCurrent",
                "currentUnreadable",
                "supplyLow",
                "lateStep",
                "driveBudget",
                "external",
            };
            return nameIn(kind, NAMES);
        }

        const char *refusalName(Refusal reason)
        {
            static const char *const NAMES[] = {
                "none",
                "faultLatched",
                "hallNotSettled",
                "positionUnknown",
                "supplyLow",
                "supplyUnreadable",
                "currentUnreadable",
                "settingsInvalid",
                "notAPerson",
                "moving",
            };
            return nameIn(reason, NAMES);
        }

        const char *hallLevelName(HallLevel level)
        {
            static const char *const NAMES[] = {"unsettled", "low", "between", "high", "rail"};
            return nameIn(level, NAMES);
        }
    } // namespace Roof
} // namespace SQM
