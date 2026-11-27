// Spec 052 (finding F58): an accepted OpenShutter or CloseShutter that hasn't
// started yet reads Opening or Closing (Slewing true) from the moment the
// call returns; once it won't run, ShutterStatus reads where the roof is (or
// Error after a failed wait); and an Open the roof won't carry out (finding
// F39: the armed automatic close would undo it at once) is an Alpaca error
// with the reason. Through the real Alpaca router, with a backend that decides
// as RoofControl does (MoveGuard's admit and step, the shared CommandCheck,
// DomeBridge's status and answer) on the simulated roof.

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "AlpacaProtocol.h"
#include "AlpacaRouter.h"
#include "CommandCheck.h"
#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "DomeBridge.h"
#include "MoveGuard.h"
#include "RoofController.h"
#include "RoofStatus.h"
#include "WeatherHold.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using Alpaca::ShutterState;
using Roof::Command;
using Roof::Origin;
using Roof::State;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t HOLD_MS = 4000;    // autoClose.parkHoldSeconds' default
    constexpr uint32_t LIMIT_MS = 240000; // park.timeoutSeconds' default

    // RoofControl's command path, on the simulated roof (no task, no lock).
    class RoofModel
    {
    public:
        explicit RoofModel(float startMm = 0.0f)
            : bench(Roof::Settings(), startMm)
        {
            RoofIo::MoveGuardSettings s;
            s.ninaSet = true;
            s.requestPark = true;
            s.wait.limitMs = LIMIT_MS;
            s.wait.holdMs = HOLD_MS;
            guard.configure(s);
            gates.weather = Safety::Verdict::Safe;
            gates.weatherText = "safe";
            gates.parked = false;
            gates.parkText = "park not confirmed (N.I.N.A.: not at park)";
        }

        Bench bench;
        RoofIo::MoveGuard guard;
        RoofIo::GateInputs gates;
        RoofIo::CloseArmed armed;
        bool closePending = false;
        uint32_t nowMs = 0;

        RoofIo::CommandCheck check(Command command, Origin origin, bool parkAside = false) const
        {
            RoofIo::CommandContext context;
            context.parkAside = parkAside;
            context.closePending = closePending;
            context.closeArmed = armed;
            return RoofIo::checkCommand(command, origin, gates, bench.roof, context);
        }

        // RoofControl::command: a stop, or admitLocked for an Open or Close.
        Alpaca::DomeAnswer command(Command command, Origin origin)
        {
            bool accepted = true;
            std::string reason;
            std::string text;
            if (command == Command::Stop || command == Command::Abort)
            {
                const RoofIo::CommandCheck c = check(command, origin);
                accepted = c.accepted;
                if (accepted)
                {
                    closePending = false;
                    guard.stopped();
                    bench.command(command, origin);
                }
                return RoofIo::domeAnswer(command, accepted, c.reason, c.text, RoofIo::FaultRecord());
            }
            const RoofIo::CommandCheck now = check(command, origin);
            const RoofIo::CommandCheck aside = check(command, origin, true);
            const RoofIo::Admission admission = guard.admit({command, origin, 0, bench.roof.status()}, now, aside, nowMs);
            if (admission.action == RoofIo::Admit::Run)
            {
                const Roof::CommandResult result = bench.command(command, origin);
                accepted = result.accepted;
                reason = Roof::refusalName(result.reason);
            }
            else if (admission.action == RoofIo::Admit::Refuse)
            {
                accepted = false;
                reason = admission.reason;
                text = admission.text;
            }
            return RoofIo::domeAnswer(command, accepted, reason, text, RoofIo::FaultRecord());
        }

        // Spec 027's automatic close taking over (RoofControl::automaticClose): a waiting Open is called off.
        void automaticCloseTakesOver() { guard.closeWins(); }

        // RoofControl::stepGuard and the control step, every 20 ms for `ms`.
        void run(uint32_t ms)
        {
            for (uint32_t gone = 0; gone < ms; gone += bench.stepMs)
            {
                const RoofIo::ParkWait &wait = guard.wait();
                if (wait.waiting() && bench.roof.atEnd(wait.command()))
                    guard.stopped();
                const RoofIo::CommandCheck waitCheck = wait.waiting() ? check(wait.command(), wait.origin(), true) : RoofIo::CommandCheck{};
                const Command waiting = wait.command();
                const Origin from = wait.origin();
                const RoofIo::GuardStep step = guard.step(bench.roof.status(), closePending, gates, waitCheck, nowMs);
                if (step.run)
                {
                    const RoofIo::CommandCheck c = check(waiting, from);
                    if (!c.accepted || !bench.command(waiting, from).accepted)
                        guard.runRefused(c.text);
                }
                bench.step();
                nowMs += bench.stepMs;
            }
        }

        Alpaca::DomeStatus status() const
        {
            return RoofIo::domeStatus(bench.roof.status(), closePending, true, RoofIo::waitView(guard.wait(), nowMs));
        }
    };

    // The Alpaca side: the firmware's router, the model behind it as N.I.N.A.'s Dome.
    class ModelBackend : public Alpaca::Backend
    {
    public:
        explicit ModelBackend(RoofModel &roof)
            : roof(roof)
        {
        }
        bool alpacaEnabled() const override { return true; }
        std::vector<Alpaca::DeviceInfo> devices() const override { return {Alpaca::domeDeviceInfo()}; }
        std::string serverName() const override { return "Observatory"; }
        std::string location() const override { return ""; }
        std::string timestampUtc() const override { return ""; }
        Alpaca::DomeStatus domeStatus() const override { return roof.status(); }
        Alpaca::DomeAnswer domeCommand(Alpaca::DomeAction action) override
        {
            const RoofIo::DomeDispatch dispatch = RoofIo::domeDispatch(action);
            return roof.command(dispatch.command, dispatch.origin);
        }

    private:
        RoofModel &roof;
    };

    const Alpaca::ServerIdentity IDENTITY{"RoofRoof", "RoofRoof", "1.2.3", 0xA1B2C3D4E5F6ULL};

    struct Imager
    {
        explicit Imager(RoofModel &roof)
            : backend(roof),
              router(backend, IDENTITY)
        {
            TEST_ASSERT_EQUAL(0, errorOf(put("connected", {{"Connected", "true"}})));
        }

        ModelBackend backend;
        Alpaca::Router router;

        Alpaca::Response call(bool put, const std::string &method, std::vector<std::pair<std::string, std::string>> params = {})
        {
            Alpaca::Request request;
            request.get = !put;
            request.put = put;
            request.path = "/api/v1/dome/0/" + method;
            request.params = std::move(params);
            Alpaca::Response response;
            TEST_ASSERT_TRUE(router.handle(request, response));
            return response;
        }
        Alpaca::Response put(const std::string &method, std::vector<std::pair<std::string, std::string>> params = {})
        {
            return call(true, method, std::move(params));
        }

        static int errorOf(const Alpaca::Response &r)
        {
            const char *at = strstr(r.body.c_str(), "\"ErrorNumber\":");
            TEST_ASSERT_NOT_NULL(at);
            return atoi(at + strlen("\"ErrorNumber\":"));
        }
        static int valueOf(const Alpaca::Response &r)
        {
            const char *at = strstr(r.body.c_str(), "\"Value\":");
            TEST_ASSERT_NOT_NULL(at);
            at += strlen("\"Value\":");
            if (strncmp(at, "true", 4) == 0)
                return 1;
            if (strncmp(at, "false", 5) == 0)
                return 0;
            return atoi(at);
        }

        int shutter() { return valueOf(call(false, "shutterstatus")); }
        bool slewing() { return valueOf(call(false, "slewing")) == 1; }
    };

    int n(ShutterState s)
    {
        return static_cast<int>(s);
    }

    bool has(const Alpaca::Response &r, const char *text)
    {
        return strstr(r.body.c_str(), text) != nullptr;
    }
} // namespace

// N.I.N.A.'s Open Dome Shutter: OpenShutter, about 1 s, one ShutterStatus read.
void test_an_open_queued_behind_the_park_reads_opening_at_once(void)
{
    RoofModel roof;
    Imager nina(roof);
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter());
    TEST_ASSERT_EQUAL(0, Imager::errorOf(nina.put("openshutter")));
    TEST_ASSERT_TRUE(roof.guard.wait().waiting());
    // The very next read, before any control step: the move to come.
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
    TEST_ASSERT_TRUE(nina.slewing());
    roof.run(1000);
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
    TEST_ASSERT_TRUE(nina.slewing());
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed); // nothing moved yet
}

void test_the_park_confirmed_then_the_roof_opens_and_reads_open(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    roof.run(30000); // the mount takes its time to park
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
    roof.gates.parked = true;
    roof.gates.parkText = "parked";
    roof.run(HOLD_MS - 200); // the park must hold first
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed);
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
    roof.run(400);
    TEST_ASSERT_TRUE(roof.bench.state() == State::Opening);
    TEST_ASSERT_FALSE(roof.guard.wait().waiting());
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter()); // no blip between the wait and the move
    TEST_ASSERT_TRUE(roof.bench.runToRest());
    TEST_ASSERT_EQUAL(n(ShutterState::Open), nina.shutter());
    TEST_ASSERT_FALSE(nina.slewing());
}

// Every step from the request to the open end: never Closed once accepted.
void test_never_closed_between_the_accept_and_the_open_end(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    for (int i = 0; i < 300; ++i)
    {
        if (i == 100)
            roof.gates.parked = true;
        roof.run(100);
        const int s = nina.shutter();
        TEST_ASSERT_TRUE(s == n(ShutterState::Opening) || s == n(ShutterState::Open));
    }
}

void test_a_person_s_stop_calls_the_open_off_and_it_reads_closed(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    roof.run(2000);
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
    roof.command(Command::Stop, Origin::Person);
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter()); // at once, the truth
    TEST_ASSERT_FALSE(nina.slewing());
    roof.gates.parked = true;
    roof.run(HOLD_MS * 2);
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed); // and it never runs
}

void test_the_automatic_close_calls_the_open_off_and_it_reads_closed(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    roof.run(1000);
    roof.automaticCloseTakesOver();
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter());
    TEST_ASSERT_FALSE(nina.slewing());
}

void test_a_wait_another_check_refuses_reads_error(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    roof.run(1000);
    roof.gates.weather = Safety::Verdict::Unsafe;
    roof.gates.weatherText = "unsafe: rain";
    roof.run(40);
    TEST_ASSERT_FALSE(roof.guard.wait().waiting());
    TEST_ASSERT_EQUAL(n(ShutterState::Error), nina.shutter());
    TEST_ASSERT_FALSE(nina.slewing());
}

void test_a_wait_that_gives_up_reads_error(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    roof.run(LIMIT_MS - 1000);
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
    roof.run(2000);
    TEST_ASSERT_EQUAL(n(ShutterState::Error), nina.shutter());
}

void test_an_open_refused_for_the_weather_is_an_alpaca_error_with_the_reason(void)
{
    RoofModel roof;
    roof.gates.weather = Safety::Verdict::Unsafe;
    roof.gates.weatherText = "unsafe: rain";
    Imager nina(roof);
    const Alpaca::Response r = nina.put("openshutter");
    TEST_ASSERT_EQUAL(Alpaca::ALPACA_ERR_INVALID_OPERATION, Imager::errorOf(r));
    TEST_ASSERT_TRUE(has(r, "\"ErrorMessage\":\"Weather not safe: unsafe: rain\""));
    TEST_ASSERT_FALSE(roof.guard.wait().waiting()); // no wait, no park request
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter());
}

void test_an_open_that_can_t_wait_for_the_park_is_an_alpaca_error(void)
{
    RoofModel roof;
    RoofIo::MoveGuardSettings off;
    off.ninaSet = true;
    off.requestPark = false; // park requests off: nobody will park the mount
    roof.guard.configure(off);
    Imager nina(roof);
    const Alpaca::Response r = nina.put("openshutter");
    TEST_ASSERT_EQUAL(Alpaca::ALPACA_ERR_INVALID_OPERATION, Imager::errorOf(r));
    TEST_ASSERT_TRUE(has(r, "asking N.I.N.A. to park is off"));
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter());
}

// Finding F39: the automatic close is armed (the weather hold fired, the
// weather safe again but not yet for the hold's length).
void test_an_open_the_armed_close_would_undo_is_an_alpaca_error(void)
{
    RoofModel roof;
    roof.gates.parked = true;
    roof.armed = {true, 42000, 60000};
    Imager nina(roof);
    const Alpaca::Response r = nina.put("openshutter");
    TEST_ASSERT_EQUAL(Alpaca::ALPACA_ERR_INVALID_OPERATION, Imager::errorOf(r));
    TEST_ASSERT_TRUE(
        has(r,
            "\"ErrorMessage\":\"The automatic close is still armed after unsafe weather and would close the roof again at once: an "
            "imaging app can't open it until the weather has been safe for 60 s (42 s left)\""));
    roof.run(200);
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed);
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter());
    // Not parked as well: refused, never a wait for a park it couldn't use.
    roof.gates.parked = false;
    TEST_ASSERT_EQUAL(Alpaca::ALPACA_ERR_INVALID_OPERATION, Imager::errorOf(nina.put("openshutter")));
    TEST_ASSERT_FALSE(roof.guard.wait().waiting());
    // Once the hold has cleared, the same Open runs.
    roof.armed = RoofIo::CloseArmed();
    roof.gates.parked = true;
    TEST_ASSERT_EQUAL(0, Imager::errorOf(nina.put("openshutter")));
    TEST_ASSERT_EQUAL(n(ShutterState::Opening), nina.shutter());
}

void test_the_armed_close_refuses_mqtt_too_and_never_a_person_or_a_close(void)
{
    RoofModel roof;
    roof.gates.parked = true;
    roof.armed = {true, 1000, 60000};
    const RoofIo::CommandCheck mqtt = roof.check(Command::Open, Origin::Mqtt);
    TEST_ASSERT_FALSE(mqtt.accepted);
    TEST_ASSERT_EQUAL_STRING(RoofIo::CLOSE_ARMED_REASON, mqtt.reason.c_str());
    TEST_ASSERT_TRUE(mqtt.text.find("an MQTT command can't open it") != std::string::npos);
    TEST_ASSERT_TRUE(roof.check(Command::Open, Origin::Person).accepted); // F39b is the owner's
    RoofModel open(1850.0f);
    open.gates.parked = true;
    open.armed = roof.armed;
    TEST_ASSERT_TRUE(open.check(Command::Close, Origin::Client).accepted);
    TEST_ASSERT_TRUE(open.check(Command::Open, Origin::Client).accepted); // already open: nothing to do
}

void test_with_the_weather_unsafe_the_gate_names_the_weather(void)
{
    RoofModel roof;
    roof.armed = {true, 60000, 60000};
    roof.gates.weather = Safety::Verdict::Unsafe;
    roof.gates.weatherText = "unsafe: cloud";
    const RoofIo::CommandCheck c = roof.check(Command::Open, Origin::Client);
    TEST_ASSERT_EQUAL_STRING("weatherNotSafe", c.reason.c_str());
}

// The hold firing while an Open waits for the park: the wait ends, Error.
void test_the_close_arming_during_the_wait_ends_it_with_error(void)
{
    RoofModel roof;
    Imager nina(roof);
    nina.put("openshutter");
    roof.run(1000);
    roof.armed = {true, 60000, 60000};
    roof.run(40);
    TEST_ASSERT_FALSE(roof.guard.wait().waiting());
    TEST_ASSERT_EQUAL(n(ShutterState::Error), nina.shutter());
    roof.gates.parked = true;
    roof.run(HOLD_MS * 2);
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closed);
}

void test_a_close_queued_behind_the_park_reads_closing_then_closed(void)
{
    RoofModel roof(1850.0f);
    Imager nina(roof);
    TEST_ASSERT_EQUAL(n(ShutterState::Open), nina.shutter());
    TEST_ASSERT_EQUAL(0, Imager::errorOf(nina.put("closeshutter")));
    TEST_ASSERT_EQUAL(n(ShutterState::Closing), nina.shutter());
    TEST_ASSERT_TRUE(nina.slewing());
    roof.run(5000);
    TEST_ASSERT_EQUAL(n(ShutterState::Closing), nina.shutter());
    TEST_ASSERT_TRUE(roof.bench.state() == State::Open);
    roof.gates.parked = true;
    roof.run(HOLD_MS + 100);
    TEST_ASSERT_TRUE(roof.bench.state() == State::Closing);
    TEST_ASSERT_TRUE(roof.bench.runToRest());
    TEST_ASSERT_EQUAL(n(ShutterState::Closed), nina.shutter());
}

void test_a_queued_close_stopped_by_a_person_reads_open(void)
{
    RoofModel roof(1850.0f);
    Imager nina(roof);
    nina.put("closeshutter");
    roof.run(1000);
    roof.command(Command::Stop, Origin::Person);
    TEST_ASSERT_EQUAL(n(ShutterState::Open), nina.shutter());
    TEST_ASSERT_FALSE(nina.slewing());
}

void test_a_close_waiting_for_an_upload_reads_closing(void)
{
    RoofModel roof(1850.0f);
    roof.closePending = true;
    Imager nina(roof);
    TEST_ASSERT_EQUAL(n(ShutterState::Closing), nina.shutter());
    TEST_ASSERT_TRUE(nina.slewing());
}

// The weather hold's time left to clear, for the refusal's text.
void test_the_hold_says_how_long_until_it_clears(void)
{
    CloseSeq::WeatherHold hold;
    CloseSeq::WeatherHold::Step step;
    step.holdMs = 60000;
    step.atOnce = true; // rain fires it at once
    step.nowMs = 0;
    hold.update(step);
    TEST_ASSERT_TRUE(hold.fired());
    TEST_ASSERT_EQUAL_UINT32(60000, static_cast<uint32_t>(hold.clearsInMs()));
    TEST_ASSERT_EQUAL_UINT32(60000, static_cast<uint32_t>(hold.lengthMs()));
    step.atOnce = false;
    step.weatherSafe = true;
    for (uint64_t t = 1000; t <= 18000; t += 1000)
    {
        step.nowMs = t;
        hold.update(step);
    }
    TEST_ASSERT_EQUAL_UINT32(42000, static_cast<uint32_t>(hold.clearsInMs()));
    step.weatherSafe = false; // a blip: the whole spell again
    step.nowMs = 19000;
    hold.update(step);
    TEST_ASSERT_EQUAL_UINT32(60000, static_cast<uint32_t>(hold.clearsInMs()));
    step.weatherSafe = true;
    for (uint64_t t = 20000; t <= 80000; t += 1000)
    {
        step.nowMs = t;
        hold.update(step);
    }
    TEST_ASSERT_FALSE(hold.fired());
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(hold.clearsInMs()));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_open_queued_behind_the_park_reads_opening_at_once);
    RUN_TEST(test_the_park_confirmed_then_the_roof_opens_and_reads_open);
    RUN_TEST(test_never_closed_between_the_accept_and_the_open_end);
    RUN_TEST(test_a_person_s_stop_calls_the_open_off_and_it_reads_closed);
    RUN_TEST(test_the_automatic_close_calls_the_open_off_and_it_reads_closed);
    RUN_TEST(test_a_wait_another_check_refuses_reads_error);
    RUN_TEST(test_a_wait_that_gives_up_reads_error);
    RUN_TEST(test_an_open_refused_for_the_weather_is_an_alpaca_error_with_the_reason);
    RUN_TEST(test_an_open_that_can_t_wait_for_the_park_is_an_alpaca_error);
    RUN_TEST(test_an_open_the_armed_close_would_undo_is_an_alpaca_error);
    RUN_TEST(test_the_armed_close_refuses_mqtt_too_and_never_a_person_or_a_close);
    RUN_TEST(test_with_the_weather_unsafe_the_gate_names_the_weather);
    RUN_TEST(test_the_close_arming_during_the_wait_ends_it_with_error);
    RUN_TEST(test_a_close_queued_behind_the_park_reads_closing_then_closed);
    RUN_TEST(test_a_queued_close_stopped_by_a_person_reads_open);
    RUN_TEST(test_a_close_waiting_for_an_upload_reads_closing);
    RUN_TEST(test_the_hold_says_how_long_until_it_clears);
    return UNITY_END();
}
