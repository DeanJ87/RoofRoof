#pragma once

// What the router's source files share: the reply envelope and one device
// request (AlpacaRouter.cpp for the common members, AlpacaDome.cpp for the Dome).

#include "AlpacaProtocol.h"
#include "AlpacaRouter.h"

#include <ArduinoJson.h>
#include <optional>
#include <string>

namespace SQM
{
    namespace Alpaca
    {
        namespace Internal
        {
            constexpr const char *DRIVER_INFO = "RoofRoof ESP32 firmware, native Alpaca";
            constexpr const char *DISABLED_MESSAGE = "Alpaca support is disabled in device settings";
            constexpr const char *BAD_REQUEST = "Invalid Alpaca device type, device number, method or HTTP verb";

            // Alpaca parameter names are case-insensitive in a GET query
            // string, case-sensitive in a PUT form body.
            inline const std::string *findParam(const Request &request, const char *name)
            {
                for (const auto &param : request.params)
                    if (paramNameMatches(param.first, name, request.put))
                        return &param.second;
                return nullptr;
            }

            inline Response badRequest(const char *message)
            {
                Response response;
                response.status = 400;
                response.contentType = "text/plain";
                response.body = message;
                return response;
            }

            // Every Alpaca reply carries the same transaction/error fields.
            class Envelope
            {
            public:
                Envelope(const Request &request, uint32_t &serverTransactionId, size_t capacity = 384)
                    : doc(capacity),
                      request(request),
                      serverTransactionId(serverTransactionId)
                {
                }

                template <typename T> void setValue(const T &value) { doc["Value"] = value; }
                JsonArray valueArray() { return doc.createNestedArray("Value"); }
                JsonObject valueObject() { return doc.createNestedObject("Value"); }

                Response finish(int errorNumber = 0, const std::string &errorMessage = "")
                {
                    const std::string *txn = findParam(request, "ClientTransactionID");
                    doc["ClientTransactionID"] = txn != nullptr ? parseClientTransactionId(*txn) : 0;
                    doc["ServerTransactionID"] = ++serverTransactionId;
                    doc["ErrorNumber"] = errorNumber;
                    doc["ErrorMessage"] = errorMessage;
                    Response response;
                    serializeJson(doc, response.body);
                    return response;
                }

            private:
                DynamicJsonDocument doc;
                const Request &request;
                uint32_t &serverTransactionId;
            };

            // One device request: what was asked, of which device, and the
            // router state it can change.
            struct DeviceCall
            {
                const Request &request;
                Backend &backend;
                const ServerIdentity &identity;
                uint32_t &serverTransactionId;
                DeviceActivity &activity;
                const DeviceInfo &info;
                std::string method;
                bool get;
                bool put;
                bool enabled;

                Envelope reply(size_t capacity = 384) const { return Envelope(request, serverTransactionId, capacity); }
                bool is(const char *name) const { return method == name; }
            };

            // The Dome's own members (AlpacaDome.cpp); nullopt for a method it doesn't have.
            std::optional<Response> domeMethod(DeviceCall &call);
        } // namespace Internal
    } // namespace Alpaca
} // namespace SQM
