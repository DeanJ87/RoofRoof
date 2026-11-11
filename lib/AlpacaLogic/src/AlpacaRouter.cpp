#include "AlpacaRouter.h"

#include "AlpacaProtocol.h"
#include "RouterInternal.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <optional>

namespace SQM
{
    namespace Alpaca
    {
        namespace
        {
            using namespace Internal;

            struct DevicePath
            {
                std::string type;
                std::string number;
                std::string method;
            };

            // /api/v1/<type>/<number>/<method>
            bool parseDevicePath(const std::string &path, DevicePath &out)
            {
                const size_t typeStart = 8; // strlen("/api/v1/")
                const size_t numberSlash = path.find('/', typeStart);
                const size_t methodSlash = numberSlash == std::string::npos ? std::string::npos : path.find('/', numberSlash + 1);
                if (methodSlash == std::string::npos || path.find('/', methodSlash + 1) != std::string::npos)
                    return false;
                out.type = path.substr(typeStart, numberSlash - typeStart);
                out.number = path.substr(numberSlash + 1, methodSlash - numberSlash - 1);
                out.method = path.substr(methodSlash + 1);
                return !out.method.empty();
            }

            // The index of the device the path names, or MAX_DEVICES if none.
            size_t findDevice(const std::vector<DeviceInfo> &devices, const DevicePath &path)
            {
                for (size_t i = 0; i < devices.size() && i < MAX_DEVICES; ++i)
                    if (devices[i].typeKey == path.type && std::to_string(devices[i].number) == path.number)
                        return i;
                return MAX_DEVICES;
            }

            // The device's own web UI (the Alpaca page's live state) tags its
            // requests source=ui: it isn't an imaging app watching the device.
            void recordActivity(const Request &request, DeviceActivity &activity)
            {
                const std::string *source = findParam(request, "source");
                if (source != nullptr && *source == "ui")
                    return;
                ++activity.requests;
                if (const std::string *clientId = findParam(request, "ClientID"))
                {
                    activity.hasClientId = true;
                    activity.clientId = parseClientTransactionId(*clientId);
                }
            }

            void setConnected(DeviceActivity &activity, bool value)
            {
                if (activity.connected && !value)
                    ++activity.disconnects;
                activity.connected = value;
            }

            // --- Common ASCOM device API ---

            std::optional<Response> commonGet(DeviceCall &call)
            {
                Envelope reply = call.reply();
                if (call.is("connected"))
                    reply.setValue(call.enabled && call.activity.connected);
                // Platform 7 asynchronous connect completes instantly, so
                // Connecting is always false.
                else if (call.is("connecting"))
                    reply.setValue(false);
                else if (call.is("name"))
                    reply.setValue(call.info.name);
                else if (call.is("description"))
                    reply.setValue(call.info.description);
                else if (call.is("driverinfo"))
                    reply.setValue(DRIVER_INFO);
                else if (call.is("driverversion"))
                    reply.setValue(call.identity.version);
                else if (call.is("interfaceversion"))
                    reply.setValue(call.info.interfaceVersion);
                else if (call.is("supportedactions"))
                    reply.valueArray();
                else
                    return std::nullopt;
                return reply.finish();
            }

            std::optional<Response> commonPut(DeviceCall &call)
            {
                Envelope reply = call.reply();
                if (call.is("connected"))
                {
                    const std::string *param = findParam(call.request, "Connected");
                    bool value = false;
                    if (param == nullptr || !parseAlpacaBool(*param, value))
                        return badRequest("Missing or invalid Connected parameter (expected true or false)");
                    if (value && !call.enabled)
                        return reply.finish(ALPACA_ERR_NOT_CONNECTED, DISABLED_MESSAGE);
                    setConnected(call.activity, value);
                    return reply.finish();
                }
                if (call.is("connect"))
                {
                    if (!call.enabled)
                        return reply.finish(ALPACA_ERR_NOT_CONNECTED, DISABLED_MESSAGE);
                    setConnected(call.activity, true);
                    return reply.finish();
                }
                if (call.is("disconnect"))
                {
                    setConnected(call.activity, false);
                    return reply.finish();
                }
                // No custom actions or raw commands are supported.
                if (call.is("action") || call.is("commandblind") || call.is("commandbool") || call.is("commandstring"))
                    return reply.finish(ALPACA_ERR_NOT_IMPLEMENTED, "Custom actions and commands are not supported");
                return std::nullopt;
            }

            std::optional<Response> commonMethod(DeviceCall &call)
            {
                if (call.get)
                    return commonGet(call);
                if (call.put)
                    return commonPut(call);
                return std::nullopt;
            }

        } // namespace

        std::string formatLocation(double latitude, double longitude)
        {
            char text[48];
            snprintf(text, sizeof(text), "%.4f, %.4f", latitude, longitude);
            return text;
        }

        Router::Router(Backend &backend, ServerIdentity identity)
            : backend(backend),
              identity(std::move(identity))
        {
        }

        bool Router::anyConnected() const
        {
            for (const DeviceActivity &activity : activityByDevice)
                if (activity.connected)
                    return true;
            return false;
        }

        void Router::resetConnections()
        {
            for (DeviceActivity &activity : activityByDevice)
                activity.connected = false;
        }

        void Router::restoreConnections(const bool (&connected)[MAX_DEVICES])
        {
            for (size_t i = 0; i < MAX_DEVICES; ++i)
                activityByDevice[i].connected = connected[i];
        }

        void Router::connectedDevices(bool (&connected)[MAX_DEVICES]) const
        {
            for (size_t i = 0; i < MAX_DEVICES; ++i)
                connected[i] = activityByDevice[i].connected;
        }

        bool Router::handle(const Request &request, Response &response)
        {
            const std::string &path = request.path;
            if (path == "/management/apiversions" && request.get)
            {
                Envelope reply(request, serverTransactionId);
                reply.valueArray().add(1);
                response = reply.finish();
                return true;
            }
            if (path == "/management/v1/description" && request.get)
            {
                Envelope reply(request, serverTransactionId);
                JsonObject value = reply.valueObject();
                // ServerName is this server's name - the device's (Alpaca
                // management API); Manufacturer stays the product.
                const std::string name = backend.serverName();
                value["ServerName"] = name.empty() ? identity.serverName : name;
                value["Manufacturer"] = identity.manufacturer;
                value["ManufacturerVersion"] = identity.version;
                value["Location"] = backend.location();
                response = reply.finish();
                return true;
            }
            if (path == "/management/v1/configureddevices" && request.get)
            {
                Envelope reply(request, serverTransactionId, 768);
                JsonArray value = reply.valueArray();
                if (backend.alpacaEnabled())
                {
                    for (const DeviceInfo &info : backend.devices())
                    {
                        JsonObject item = value.createNestedObject();
                        item["DeviceName"] = info.name;
                        item["DeviceType"] = info.type;
                        item["DeviceNumber"] = info.number;
                        item["UniqueID"] = buildUniqueId(identity.mac, info.typeKey, info.number);
                    }
                }
                response = reply.finish();
                return true;
            }
            if (path.rfind("/api/v1/", 0) == 0)
            {
                response = device(request);
                return true;
            }
            if (path.rfind("/management/", 0) == 0)
            {
                response = badRequest(BAD_REQUEST);
                return true;
            }
            return false;
        }

        Response Router::device(const Request &request)
        {
            DevicePath path;
            if (!parseDevicePath(request.path, path))
                return badRequest(BAD_REQUEST);
            const std::vector<DeviceInfo> devices = backend.devices();
            const size_t index = findDevice(devices, path);
            if (index >= MAX_DEVICES)
                return badRequest(BAD_REQUEST);
            DeviceCall call{
                request,
                backend,
                identity,
                serverTransactionId,
                activityByDevice[index],
                devices[index],
                path.method,
                request.get,
                request.put,
                backend.alpacaEnabled()}; // dep: D-20
            recordActivity(request, call.activity);
            if (std::optional<Response> common = commonMethod(call))
                return *common;
            if (call.info.typeKey == "dome")
                if (std::optional<Response> dome = domeMethod(call))
                    return *dome;
            return badRequest(BAD_REQUEST);
        }

    } // namespace Alpaca
} // namespace SQM
