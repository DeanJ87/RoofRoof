#pragma once

#include "AlpacaDome.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace SQM
{
    namespace Alpaca
    {
        // The Alpaca HTTP API (management + device routes) independent of any
        // web server, so the firmware and the native simulator that ConformU
        // tests in CI run the same code. The devices come from the Backend.

        struct Request
        {
            bool get = false;
            bool put = false;
            std::string path; // e.g. /api/v1/dome/0/shutterstatus
            // Query string (GET) or form body (PUT) parameters, URL-decoded.
            std::vector<std::pair<std::string, std::string>> params;
        };

        struct Response
        {
            int status = 200;
            const char *contentType = "application/json";
            std::string body;
        };

        // The most Alpaca devices one server offers (fixed arrays: activity,
        // client watch, connection memory). The Dome is the only one (spec 006).
        constexpr size_t MAX_DEVICES = 2;

        // One Alpaca device as the management API lists it.
        struct DeviceInfo
        {
            std::string type;    // Alpaca DeviceType, e.g. "Dome"
            std::string typeKey; // lowercase, as in routes and UniqueIDs, e.g. "dome"
            uint32_t number = 0;
            std::string name;
            std::string description;
            int interfaceVersion = 1;
        };

        // The one Dome the device offers: number 0, interface version 3 (spec 006).
        DeviceInfo domeDeviceInfo();

        // What the router needs from the device.
        class Backend
        {
        public:
            virtual ~Backend() = default;
            virtual bool alpacaEnabled() const = 0;
            // The devices offered, at most MAX_DEVICES, in a fixed order: a
            // device's index is its slot in DeviceActivity and ClientWatch.
            virtual std::vector<DeviceInfo> devices() const = 0;
            virtual std::string serverName() const = 0;   // the device's name (Settings → Device → Name)
            virtual std::string location() const = 0;     // "lat, lon" of the site, or "" (formatLocation)
            virtual std::string timestampUtc() const = 0; // ISO 8601, or "" if the clock isn't set
            // The Dome (spec 006): the shutter now, and an action. The router
            // calls domeCommand only with Alpaca on, and for open and close only
            // for a connected client; the backend applies the roof's own checks.
            virtual DomeStatus domeStatus() const = 0;
            virtual DomeAnswer domeCommand(DomeAction action) = 0;
        };

        struct ServerIdentity
        {
            std::string serverName;
            std::string manufacturer;
            std::string version;
            uint64_t mac = 0; // for UniqueIDs
        };

        // The management description's Location: where the device is, as
        // "lat, lon" in machine format ('.' decimals, 4 places).
        std::string formatLocation(double latitude, double longitude);

        // What clients did with one device since the Router started. The
        // counters only ever grow; ClientWatch compares them between passes,
        // so the Router (which runs on the web server's task) needs no clock.
        struct DeviceActivity
        {
            bool connected = false;
            uint32_t requests = 0;    // every device request, any method (except the web UI's, source=ui)
            uint32_t disconnects = 0; // clean disconnects of a connected device
            bool hasClientId = false;
            uint32_t clientId = 0; // ClientID of the latest request that sent one
        };

        class Router
        {
        public:
            Router(Backend &backend, ServerIdentity identity);

            // Handles /management/... and /api/v1/...; false for any other path.
            bool handle(const Request &request, Response &response);

            // A client (N.I.N.A.) currently has any device connected.
            bool anyConnected() const;

            // Activity of the device at `index` (Backend::devices order).
            DeviceActivity activity(size_t index) const { return index < MAX_DEVICES ? activityByDevice[index] : DeviceActivity{}; }

            // As after a restart: no device connected (the demo's restart).
            void resetConnections();

            // After a restart the device caused itself (ConnectionMemory): the
            // imaging app's connections as they were, without counting a
            // disconnect.
            void restoreConnections(const bool (&connected)[MAX_DEVICES]);

            // Which devices a client has connected, in Backend::devices order.
            void connectedDevices(bool (&connected)[MAX_DEVICES]) const;

        private:
            Response device(const Request &request);

            Backend &backend;
            ServerIdentity identity;
            DeviceActivity activityByDevice[MAX_DEVICES];
            uint32_t serverTransactionId = 0;
        };

    } // namespace Alpaca
} // namespace SQM
