// Alpaca simulator: serves the firmware's Alpaca API (Alpaca::Router from
// lib/AlpacaLogic) on a desktop machine, so the protocol can be tested without
// an ESP32. The Dome (spec 006) is spec 002's motion logic driving a simulated
// roof (test/roof_sim), through the same gate checks and the same Dome mapping
// (lib/RoofIoLogic DomeBridge) as the firmware; ConformU runs against it in CI.
//
//   tools/alpaca-sim/build.sh && ./alpaca-sim [port] [unsafe] [unparked]
//
// Starts closed, with the weather safe and the mount parked unless told
// otherwise. Travel is 300 mm at 100 mm/s, so a move takes about 4 s. The roof
// is stepped every 20 ms of simulated time, caught up to the wall clock before
// each request.
//
// Deliberately tiny: one request per connection, GET query strings and
// application/x-www-form-urlencoded PUT bodies, nothing else.

#include "AlpacaRouter.h"
#include "CommandCheck.h"
#include "Config.h"
#include "DomeBridge.h"
#include "RoofGates.h"
#include "RoofSim.h"
#include "RoofText.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

using namespace SQM;
using namespace SQM::Alpaca;

namespace
{
    Roof::Settings simulatedSettings()
    {
        Roof::Settings settings;
        settings.travelMm = 300;
        settings.speedMmPerSecond = 100.0f;
        settings.timeoutMarginMs = 5000;
        settings.leaveMagnetMs = 2000;
        settings.rampUpMs = 500;
        settings.rampDownMs = 500;
        return settings;
    }

    // The roof: the controller and the plant, with fixed weather and park inputs.
    class SimRoof
    {
    public:
        SimRoof(bool safe, bool parked)
            : bench(simulatedSettings()),
              started(std::chrono::steady_clock::now())
        {
            gates.weather = safe ? Safety::Verdict::Safe : Safety::Verdict::Unsafe;
            gates.weatherText = safe ? "safe" : "unsafe: rain (simulated)";
            gates.parked = parked;
            gates.parkText = parked ? "parked" : "N.I.N.A. reports the mount not parked (simulated)";
            bench.sim.speedMmPerSecond = simulatedSettings().speedMmPerSecond; // the plant moves as the settings say
            steppedMs = 0;
        }

        DomeStatus status()
        {
            catchUp();
            return RoofIo::domeStatus(bench.roof.status(), false, true);
        }

        // The firmware's decision (RoofIo::checkCommand, as RoofControl::precheck),
        // then the motion logic. Not simulated: uploads and MQTT (so no close
        // waits), the automatic close, and Alpaca switched off.
        DomeAnswer command(DomeAction action)
        {
            catchUp();
            const RoofIo::DomeDispatch dispatch = RoofIo::domeDispatch(action);
            const Roof::Command command = dispatch.command;
            // No automatic close and no waiting close in the simulator.
            const RoofIo::CommandCheck check = RoofIo::checkCommand(command, dispatch.origin, gates, bench.roof, RoofIo::CommandContext());
            bool accepted = check.accepted;
            std::string reason = check.reason;
            std::string text = check.text;
            if (accepted)
            {
                const Roof::CommandResult result = bench.roof.command(command, dispatch.origin);
                accepted = result.accepted;
                reason = Roof::refusalName(result.reason);
                text = RoofIo::describeRefusal(result);
            }
            RoofIo::FaultRecord fault;
            fault.fault = bench.roof.status().fault;
            const DomeAnswer answer = RoofIo::domeAnswer(command, accepted, reason, text, fault);
            std::printf(
                "  roof %s: %s%s\n", Roof::commandName(command), answer.accepted ? "accepted" : "refused: ", answer.message.c_str());
            return answer;
        }

    private:
        // Steps the roof up to the wall clock, applying the firmware's
        // interlock and open stop after each step (RoofControl::checkGates).
        void catchUp()
        {
            const auto elapsed = std::chrono::steady_clock::now() - started;
            const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            while (steppedMs + bench.stepMs <= nowMs)
            {
                bench.step();
                steppedMs += bench.stepMs;
                const Roof::Status &now = bench.roof.status();
                if (RoofIo::interlockLost(now, gates))
                    bench.roof.raiseFault(
                        RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, static_cast<float>(RoofIo::interlockBits(gates)), 0.0f)
                            .fault);
                else if (RoofIo::openMustStop(now, gates))
                    bench.roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            }
        }

        RoofTest::Bench bench;
        RoofIo::GateInputs gates;
        std::chrono::steady_clock::time_point started;
        uint64_t steppedMs;
    };

    class SimBackend : public Backend
    {
    public:
        explicit SimBackend(SimRoof &roof)
            : roof(roof)
        {
        }
        bool alpacaEnabled() const override { return true; }
        std::vector<DeviceInfo> devices() const override { return {domeDeviceInfo()}; }
        std::string serverName() const override { return "RoofRoof simulator"; }
        std::string location() const override { return ""; }
        std::string timestampUtc() const override
        {
            const time_t now = time(nullptr);
            struct tm utc;
            gmtime_r(&now, &utc);
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
            return buffer;
        }
        DomeStatus domeStatus() const override { return roof.status(); }
        DomeAnswer domeCommand(DomeAction action) override { return roof.command(action); }

    private:
        SimRoof &roof;
    };

    std::string urlDecode(const std::string &in)
    {
        std::string out;
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '+')
                out += ' ';
            else if (in[i] == '%' && i + 2 < in.size())
            {
                out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            }
            else
                out += in[i];
        }
        return out;
    }

    void parseParams(const std::string &text, Request &request)
    {
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('&', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string pair = text.substr(start, end - start);
            const size_t eq = pair.find('=');
            if (!pair.empty())
                request.params.emplace_back(urlDecode(pair.substr(0, eq)), eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1)));
            start = end + 1;
        }
    }

    void sendAll(int fd, const std::string &data)
    {
        size_t sent = 0;
        while (sent < data.size())
        {
            const ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n <= 0)
                return;
            sent += static_cast<size_t>(n);
        }
    }

    void serve(int client, Router &router)
    {
        std::string raw;
        char buffer[4096];
        size_t headerEnd = std::string::npos;
        while (headerEnd == std::string::npos)
        {
            const ssize_t n = recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0)
                return;
            raw.append(buffer, static_cast<size_t>(n));
            headerEnd = raw.find("\r\n\r\n");
        }

        size_t contentLength = 0;
        const std::string headers = raw.substr(0, headerEnd);
        for (size_t line = headers.find("\r\n"); line != std::string::npos; line = headers.find("\r\n", line + 2))
        {
            const size_t next = headers.find("\r\n", line + 2);
            std::string header = headers.substr(line + 2, next == std::string::npos ? std::string::npos : next - line - 2);
            for (char &c : header)
                c = static_cast<char>(tolower(c));
            if (header.rfind("content-length:", 0) == 0)
                contentLength = std::strtoul(header.c_str() + 15, nullptr, 10);
        }
        while (raw.size() < headerEnd + 4 + contentLength)
        {
            const ssize_t n = recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0)
                break;
            raw.append(buffer, static_cast<size_t>(n));
        }

        // Request line: METHOD /path?query HTTP/1.1
        const size_t methodEnd = raw.find(' ');
        const size_t targetEnd = raw.find(' ', methodEnd + 1);
        const std::string method = raw.substr(0, methodEnd);
        const std::string target = raw.substr(methodEnd + 1, targetEnd - methodEnd - 1);

        Request request;
        request.get = method == "GET";
        request.put = method == "PUT";
        const size_t question = target.find('?');
        request.path = target.substr(0, question);
        if (question != std::string::npos)
            parseParams(target.substr(question + 1), request);
        if (request.put)
            parseParams(raw.substr(headerEnd + 4, contentLength), request);

        Response response;
        if (!router.handle(request, response))
        {
            response.status = 404;
            response.contentType = "text/plain";
            response.body = "Not found";
        }
        const char *reason = response.status == 200 ? "OK" : response.status == 400 ? "Bad Request" : "Not Found";
        std::string reply = "HTTP/1.1 " + std::to_string(response.status) + " " + reason + "\r\n" +
                            "Content-Type: " + response.contentType + "\r\n" + "Content-Length: " + std::to_string(response.body.size()) +
                            "\r\n" + "Connection: close\r\n\r\n" + response.body;
        sendAll(client, reply);
        std::printf("%s %s -> %d\n", method.c_str(), target.c_str(), response.status);
        std::fflush(stdout);
    }
} // namespace

int main(int argc, char **argv)
{
    const int port = argc > 1 ? std::atoi(argv[1]) : 11111;
    bool safe = true;
    bool parked = true;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "unsafe") == 0)
            safe = false;
        else if (std::strcmp(argv[i], "unparked") == 0)
            parked = false;
        else
        {
            std::fprintf(stderr, "alpaca-sim: unknown option %s (expected unsafe or unparked)\n", argv[i]);
            return 2;
        }
    }
    std::signal(SIGPIPE, SIG_IGN);

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(server, 16) != 0)
    {
        std::perror("alpaca-sim: bind/listen");
        return 1;
    }
    std::printf(
        "Alpaca simulator on http://127.0.0.1:%d (weather %s, mount %s)\n",
        port,
        safe ? "safe" : "unsafe",
        parked ? "parked" : "not parked");
    std::fflush(stdout);

    SimRoof roof(safe, parked);
    SimBackend backend(roof);
    Router router(backend, {"RoofRoof (simulator)", "RoofRoof", "sim", 0x0000AABBCCDDEEFFULL});
    for (;;)
    {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0)
            continue;
        serve(client, router);
        close(client);
    }
}
