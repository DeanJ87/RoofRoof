#include "AlpacaDome.h"

#include "RouterInternal.h"

// The Dome's members (spec 006, contracts/alpaca-dome.md): a shutter that
// opens, closes and stops, and nothing else. What the roof does with a
// command is the backend's (the same checks as the web UI's, constitution
// II-III); this decides only what the Alpaca protocol needs: which members
// exist, the connection and the replies.

namespace SQM
{
    namespace Alpaca
    {
        DeviceInfo domeDeviceInfo()
        {
            return {"Dome", "dome", 0, "RoofRoof roof", "Roll-off roof: shutter open, close and stop only", 3};
        }

        namespace Internal
        {
            namespace
            {
                const char *const NOT_CONNECTED_MESSAGE = "Not connected: connect to the Dome first";
                const char *const NOT_IMPLEMENTED_MESSAGE = "Not implemented: this Dome is a roll-off roof with a shutter only";

                // Read members that are always false (constitution IV: every
                // capability but CanSetShutter).
                const char *const FALSE_MEMBERS[] = {
                    "canfindhome",
                    "canpark",
                    "cansetaltitude",
                    "cansetazimuth",
                    "cansetpark",
                    "canslave",
                    "cansyncazimuth",
                    "slaved",
                };
                // Read members the Dome doesn't have.
                const char *const MISSING_PROPERTIES[] = {"altitude", "azimuth", "athome", "atpark"};
                // Methods the Dome doesn't have.
                const char *const MISSING_METHODS[] = {"findhome", "park", "setpark", "slewtoaltitude", "slewtoazimuth", "synctoazimuth"};

                template <size_t N> bool isOneOf(const DeviceCall &call, const char *const (&names)[N])
                {
                    for (const char *name : names)
                        if (call.is(name))
                            return true;
                    return false;
                }

                Response deviceState(DeviceCall &call)
                {
                    Envelope reply = call.reply(512);
                    JsonArray state = reply.valueArray();
                    if (call.enabled)
                    {
                        const DomeStatus status = call.backend.domeStatus();
                        JsonObject shutter = state.createNestedObject();
                        shutter["Name"] = "ShutterStatus";
                        shutter["Value"] = static_cast<int>(status.shutter);
                        JsonObject slewing = state.createNestedObject();
                        slewing["Name"] = "Slewing";
                        slewing["Value"] = status.slewing;
                    }
                    const std::string timestamp = call.backend.timestampUtc();
                    if (!timestamp.empty())
                    {
                        JsonObject item = state.createNestedObject();
                        item["Name"] = "TimeStamp";
                        item["Value"] = timestamp;
                    }
                    return reply.finish();
                }

                // ShutterStatus and Slewing: the roof's state, or the disabled error.
                Response liveProperty(DeviceCall &call)
                {
                    Envelope reply = call.reply();
                    if (!call.enabled)
                    {
                        reply.setValue(0);
                        return reply.finish(ALPACA_ERR_NOT_CONNECTED, DISABLED_MESSAGE);
                    }
                    const DomeStatus status = call.backend.domeStatus();
                    if (call.is("shutterstatus"))
                        reply.setValue(static_cast<int>(status.shutter));
                    else
                        reply.setValue(status.slewing);
                    return reply.finish();
                }

                std::optional<Response> domeGet(DeviceCall &call)
                {
                    if (call.is("shutterstatus") || call.is("slewing"))
                        return liveProperty(call);
                    if (call.is("devicestate"))
                        return deviceState(call);
                    Envelope reply = call.reply();
                    if (call.is("cansetshutter"))
                    {
                        reply.setValue(true);
                        return reply.finish();
                    }
                    if (isOneOf(call, FALSE_MEMBERS))
                    {
                        reply.setValue(false);
                        return reply.finish();
                    }
                    if (isOneOf(call, MISSING_PROPERTIES))
                    {
                        reply.setValue(0);
                        return reply.finish(ALPACA_ERR_NOT_IMPLEMENTED, NOT_IMPLEMENTED_MESSAGE);
                    }
                    return std::nullopt;
                }

                // Open, close and abort. Open and close need Alpaca on and a
                // connected client; abort is never refused, not even with
                // Alpaca off (constitution I: stop always works).
                Response action(DeviceCall &call, DomeAction what)
                {
                    Envelope reply = call.reply();
                    if (what != DomeAction::AbortSlew && !call.enabled)
                        return reply.finish(ALPACA_ERR_NOT_CONNECTED, DISABLED_MESSAGE);
                    if (what != DomeAction::AbortSlew && !call.activity.connected)
                        return reply.finish(ALPACA_ERR_NOT_CONNECTED, NOT_CONNECTED_MESSAGE);
                    const DomeAnswer answer = call.backend.domeCommand(what);
                    if (!answer.accepted)
                        return reply.finish(ALPACA_ERR_INVALID_OPERATION, answer.message.empty() ? "Refused" : answer.message);
                    return reply.finish();
                }

                Response slavedPut(DeviceCall &call)
                {
                    const std::string *param = findParam(call.request, "Slaved");
                    bool value = false;
                    if (param == nullptr || !parseAlpacaBool(*param, value))
                        return badRequest("Missing or invalid Slaved parameter (expected true or false)");
                    Envelope reply = call.reply();
                    if (value)
                        return reply.finish(ALPACA_ERR_NOT_IMPLEMENTED, NOT_IMPLEMENTED_MESSAGE);
                    return reply.finish();
                }

                std::optional<Response> domePut(DeviceCall &call)
                {
                    if (call.is("openshutter"))
                        return action(call, DomeAction::OpenShutter);
                    if (call.is("closeshutter"))
                        return action(call, DomeAction::CloseShutter);
                    if (call.is("abortslew"))
                        return action(call, DomeAction::AbortSlew);
                    if (call.is("slaved"))
                        return slavedPut(call);
                    if (isOneOf(call, MISSING_METHODS))
                        return call.reply().finish(ALPACA_ERR_NOT_IMPLEMENTED, NOT_IMPLEMENTED_MESSAGE);
                    return std::nullopt;
                }
            } // namespace

            std::optional<Response> domeMethod(DeviceCall &call)
            {
                if (call.get)
                    return domeGet(call);
                if (call.put)
                    return domePut(call);
                return std::nullopt;
            }
        } // namespace Internal
    } // namespace Alpaca
} // namespace SQM
