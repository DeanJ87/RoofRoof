#include "AlpacaSwitch.h"

#include "RouterInternal.h"

#include <cerrno>
#include <cstdlib>

// Spec 043: the Switch's members (ISwitchV3). Every switch is a read-only
// boolean (0 or 1, step 1): what the roof is doing decides it, never a client.
// Writing one, renaming one or the asynchronous members are not implemented.
// An Id outside 0..MaxSwitch-1 is InvalidValue for every member that takes
// one (ConformU checks each); a missing or malformed parameter is HTTP 400.

namespace SQM
{
    namespace Alpaca
    {
        DeviceInfo switchDeviceInfo(const char *simulation)
        {
            std::string description = "Roll-off roof position for the telescope, read-only";
            const std::string kind = simulation != nullptr ? simulation : "off";
            if (kind == "bench")
                description = "BENCH MODE (simulated roof, no motor): " + description;
            else if (kind == "demo")
                description = "Demo (simulated roof): " + description;
            return {"Switch", "switch", 0, "RoofRoof", description, 3};
        }

        namespace Internal
        {
            namespace
            {
                const char *const READ_ONLY_MESSAGE = "Not implemented: these switches are read-only (the roof decides them)";
                const char *const NO_RENAME_MESSAGE = "Not implemented: the switch names are fixed";
                const char *const NO_ASYNC_MESSAGE = "Not implemented: these switches have no asynchronous operation";
                const char *const BAD_ID = "Missing or invalid Id parameter (expected an integer)";

                // The Id parameter: an integer (Alpaca Int16), or false.
                bool parseId(const DeviceCall &call, long &id)
                {
                    const std::string *raw = findParam(call.request, "Id");
                    if (raw == nullptr || raw->empty())
                        return false;
                    char *end = nullptr;
                    errno = 0;
                    const long value = std::strtol(raw->c_str(), &end, 10);
                    if (end == raw->c_str() || *end != '\0' || errno == ERANGE)
                        return false;
                    id = value;
                    return true;
                }

                // A member that takes an Id: nullopt with `index` set when the Id is
                // usable, else the reply (400 for a bad parameter, InvalidValue
                // out of range; with Alpaca off, the disabled error).
                std::optional<Response> withId(DeviceCall &call, const std::vector<SwitchReading> &readings, size_t &index)
                {
                    long id = 0;
                    if (!parseId(call, id))
                        return badRequest(BAD_ID);
                    if (!call.enabled)
                        return call.reply().finish(ALPACA_ERR_NOT_CONNECTED, DISABLED_MESSAGE);
                    if (id < 0 || static_cast<size_t>(id) >= readings.size())
                        return call.reply().finish(
                            ALPACA_ERR_INVALID_VALUE,
                            "Switch Id " + std::to_string(id) + " is out of range (0 to " + std::to_string(readings.size()) + " - 1)");
                    index = static_cast<size_t>(id);
                    return std::nullopt;
                }

                Response deviceState(DeviceCall &call)
                {
                    Envelope reply = call.reply(2048); // ten items and their copied names (64-bit hosts need the most)
                    JsonArray state = reply.valueArray();
                    if (call.enabled)
                    {
                        const std::vector<SwitchReading> readings = call.backend.switches();
                        for (size_t i = 0; i < readings.size(); ++i)
                        {
                            const std::string n = std::to_string(i);
                            JsonObject on = state.createNestedObject();
                            on["Name"] = "GetSwitch" + n;
                            on["Value"] = readings[i].value;
                            JsonObject value = state.createNestedObject();
                            value["Name"] = "GetSwitchValue" + n;
                            value["Value"] = readings[i].value ? 1.0 : 0.0;
                            JsonObject done = state.createNestedObject();
                            done["Name"] = "StateChangeComplete" + n;
                            done["Value"] = true;
                        }
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

                // The per-switch reads: what each answers for one switch.
                enum class Read : uint8_t
                {
                    False,
                    True,
                    State,
                    Value,
                    Name,
                    Description,
                    Zero,
                    One,
                };
                struct ReadMember
                {
                    const char *method;
                    Read read;
                };
                const ReadMember READS[] = {
                    {"canwrite", Read::False},
                    {"canasync", Read::False},
                    {"getswitch", Read::State},
                    {"getswitchvalue", Read::Value},
                    {"getswitchname", Read::Name},
                    {"getswitchdescription", Read::Description},
                    {"minswitchvalue", Read::Zero},
                    {"maxswitchvalue", Read::One},
                    {"switchstep", Read::One},
                    {"statechangecomplete", Read::True}, // nothing asynchronous ever runs
                };

                void answer(Envelope &reply, Read read, const SwitchReading &reading)
                {
                    switch (read)
                    {
                    case Read::False:
                    case Read::True:
                        reply.setValue(read == Read::True);
                        break;
                    case Read::State:
                        reply.setValue(reading.value);
                        break;
                    case Read::Value:
                        reply.setValue(reading.value ? 1.0 : 0.0);
                        break;
                    case Read::Name:
                        reply.setValue(reading.name);
                        break;
                    case Read::Description:
                        reply.setValue(reading.description);
                        break;
                    case Read::Zero:
                    case Read::One:
                        reply.setValue(read == Read::One ? 1.0 : 0.0);
                        break;
                    }
                }

                std::optional<Response> switchGet(DeviceCall &call)
                {
                    if (call.is("devicestate"))
                        return deviceState(call);
                    const std::vector<SwitchReading> readings = call.backend.switches();
                    if (call.is("maxswitch"))
                    {
                        Envelope reply = call.reply();
                        reply.setValue(static_cast<int>(readings.size()));
                        return reply.finish();
                    }
                    for (const ReadMember &member : READS)
                    {
                        if (!call.is(member.method))
                            continue;
                        size_t index = 0;
                        if (std::optional<Response> refused = withId(call, readings, index))
                            return refused;
                        Envelope reply = call.reply(512);
                        answer(reply, member.read, readings[index]);
                        return reply.finish();
                    }
                    return std::nullopt;
                }

                // Skips ASCII digits from `i`; how many.
                size_t skipDigits(const std::string &raw, size_t &i)
                {
                    const size_t from = i;
                    while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9')
                        ++i;
                    return i - from;
                }

                void skipSign(const std::string &raw, size_t &i)
                {
                    if (i < raw.size() && (raw[i] == '-' || raw[i] == '+'))
                        ++i;
                }

                // A number in Alpaca's invariant form ("1", "-0.5", "1e3"), checked by
                // its shape only: the value is never used (every write is refused), and
                // strtod would cost the firmware 5 KB of flash.
                bool looksNumeric(const std::string &raw)
                {
                    size_t i = 0;
                    skipSign(raw, i);
                    size_t digits = skipDigits(raw, i);
                    if (i < raw.size() && raw[i] == '.')
                        digits += skipDigits(raw, ++i);
                    if (digits == 0)
                        return false;
                    if (i < raw.size() && (raw[i] == 'e' || raw[i] == 'E'))
                    {
                        skipSign(raw, ++i);
                        if (skipDigits(raw, i) == 0)
                            return false;
                    }
                    return i == raw.size();
                }

                // A PUT's second parameter, checked for its form only (the call is
                // refused afterwards): false when it is missing or malformed.
                bool secondParamOk(const DeviceCall &call, const char *name, bool isBool)
                {
                    const std::string *raw = findParam(call.request, name);
                    if (raw == nullptr)
                        return false;
                    if (isBool)
                    {
                        bool ignored = false;
                        return parseAlpacaBool(*raw, ignored);
                    }
                    if (std::string(name) == "Name")
                        return true;
                    return looksNumeric(*raw);
                }

                std::optional<Response> switchPut(DeviceCall &call)
                {
                    struct Member
                    {
                        const char *method;
                        const char *param; // nullptr: the Id alone
                        bool isBool;
                        const char *message;
                    };
                    static const Member MEMBERS[] = {
                        {"setswitch", "State", true, READ_ONLY_MESSAGE},
                        {"setswitchvalue", "Value", false, READ_ONLY_MESSAGE},
                        {"setswitchname", "Name", false, NO_RENAME_MESSAGE},
                        {"setasync", "State", true, NO_ASYNC_MESSAGE},
                        {"setasyncvalue", "Value", false, NO_ASYNC_MESSAGE},
                        {"cancelasync", nullptr, false, NO_ASYNC_MESSAGE},
                    };
                    for (const Member &member : MEMBERS)
                    {
                        if (!call.is(member.method))
                            continue;
                        const std::vector<SwitchReading> readings = call.backend.switches();
                        size_t index = 0;
                        if (std::optional<Response> refused = withId(call, readings, index))
                            return refused;
                        if (member.param != nullptr && !secondParamOk(call, member.param, member.isBool))
                            return badRequest("Missing or invalid parameter for this switch member");
                        return call.reply().finish(ALPACA_ERR_NOT_IMPLEMENTED, member.message);
                    }
                    return std::nullopt;
                }
            } // namespace

            std::optional<Response> switchMethod(DeviceCall &call)
            {
                if (call.get)
                    return switchGet(call);
                if (call.put)
                    return switchPut(call);
                return std::nullopt;
            }
        } // namespace Internal
    } // namespace Alpaca
} // namespace SQM
