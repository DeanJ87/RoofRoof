// Spec 033 (constitution 9.0.0): a person's jog through the teach's start
// checks (lib/CalibrationLogic) when the Hall sensor has failed, when the end
// magnets are overridden, and toward closed in unsafe weather, daylight or
// with the mount not confirmed parked (a close always wins). The rig is spec
// 019's (test_calibration_teach).

#include <ArduinoJson.h>
#include <string>
#include <unity.h>

#include "RoofController.h"
#include "RoofGates.h"
#include "RoofPlant.h"
#include "RoofText.h"
#include "Teach.h"

using namespace SQM;
using namespace SQM::Calib;
using Roof::End;
using Roof::FaultKind;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t STEP_MS = 20;

    struct Rig
    {
        Roof::Controller roof;
        RoofPlant::Plant plant;
        Teach teach;
        RoofIo::GateInputs gates;
        uint32_t nowMs = 1000;

        explicit Rig(float startMm = 0.0f)
        {
            plant.positionMm = startMm;
            gates.weather = Safety::Verdict::Safe;
            gates.weatherText = "safe";
            gates.parked = true;
            gates.parkText = "parked";
            steps(1200);
        }

        void step()
        {
            plant.drive(roof.output(), STEP_MS);
            nowMs += STEP_MS;
            if (RoofIo::interlockLost(roof.status(), gates))
                roof.raiseFault(Roof::Fault{FaultKind::External, 1.0f, 0.0f});
            if (RoofIo::openMustStop(roof.status(), gates))
                roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            const Roof::Readings r = plant.readings(nowMs);
            roof.tick(r);
            teach.step(roof.status(), r.hallCounts);
        }

        void steps(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += STEP_MS)
                step();
        }

        StartInputs in() const
        {
            StartInputs s;
            s.nowMs = nowMs;
            s.gates = gates;
            s.faultText = "fault";
            return s;
        }

        Answer jog(End target, bool attended = true, bool calibrating = false)
        {
            return teach.jog(roof, target, attended, calibrating, in());
        }

        // Holds the jog for `ms`, renewing every 140 ms; then releases it (a Stop).
        void hold(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += STEP_MS)
            {
                if (t % 140 == 0)
                    Teach::hold(roof, roof.jogId());
                step();
            }
            roof.command(Roof::Command::Stop, Roof::Origin::Person);
        }

        Answer mark(Where where) { return teach.mark(where, roof.status(), false); }
    };

    void assertRefused(const char *reason, const Answer &answer)
    {
        TEST_ASSERT_FALSE(answer.accepted);
        TEST_ASSERT_EQUAL_STRING(reason, answer.reason.c_str());
        TEST_ASSERT_FALSE(answer.text.empty());
    }

    Stats readings(uint16_t value, uint16_t spread = 0, uint32_t count = 50)
    {
        Stats s;
        for (uint32_t i = 0; i < count; ++i)
            s.add(static_cast<uint16_t>(value - spread + (i % 2) * 2 * spread));
        return s;
    }
} // namespace

// Spec 033: the attended checks leave a Hall rail fault to the controller,
// which runs a person's jog blind; any other fault still refuses it.
void test_a_jog_runs_with_the_hall_rail_fault_only(void)
{
    Rig r(900.0f);
    r.plant.railCounts = 4095;
    r.steps(200);
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::HallRail);
    assertRefused("notAttended", r.jog(End::Closed, false));
    TEST_ASSERT_TRUE(r.jog(End::Closed).accepted);
    TEST_ASSERT_TRUE(r.roof.status().jogging);
    TEST_ASSERT_TRUE(Teach::yieldToClose(r.roof));
    TEST_ASSERT_FALSE(r.roof.status().jogging);

    Rig other(900.0f);
    other.roof.raiseFault(Roof::Fault{FaultKind::External, 0, 0});
    other.plant.railCounts = 4095;
    other.steps(200);
    assertRefused("faultLatched", other.jog(End::Closed));
}

// Spec 033: a person's override of the end magnets goes through the same attended checks.
void test_a_jog_may_override_the_end_magnets(void)
{
    Rig r(900.0f);
    StartInputs s = r.in();
    s.ignoreMagnets = true;
    assertRefused("notAttended", r.teach.jog(r.roof, End::Open, false, false, s));
    TEST_ASSERT_TRUE(r.teach.jog(r.roof, End::Open, true, false, s).accepted);
    r.hold(2000);
    TEST_ASSERT_TRUE(r.roof.magnetsDoubted());
    r.roof.raiseFault(Roof::Fault{FaultKind::External, 0, 0});
    assertRefused("faultLatched", r.teach.jog(r.roof, End::Open, true, false, s));
}

void test_a_closing_jog_runs_in_unsafe_weather_daylight_and_unparked(void)
{
    Rig r(900.0f);
    r.gates.weather = Safety::Verdict::Unsafe;
    r.gates.daylightCloses = true;
    r.gates.parked = false;
    r.gates.fittedSwitches = 1;
    r.gates.switch1 = RoofIo::Contact::Open;
    StartInputs s = r.in();
    s.sequenceIdle = false; // the automatic close waiting for the park
    TEST_ASSERT_TRUE(r.teach.jog(r.roof, End::Closed, true, false, s).accepted);
    const float before = r.plant.positionMm;
    for (int i = 0; i < 60; ++i)
    {
        Teach::hold(r.roof, r.roof.jogId());
        r.step();
    }
    TEST_ASSERT_TRUE(r.roof.status().jogging);
    TEST_ASSERT_TRUE(r.roof.status().driving);
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::None); // no park interlock fault
    TEST_ASSERT_TRUE(r.plant.positionMm < before);
    // Still a person's only, attended, and the opening one stays refused.
    assertRefused("notAttended", r.jog(End::Closed, false));
    r.roof.command(Roof::Command::Stop, Roof::Origin::Person);
    r.steps(100);
    assertRefused("closeSequence", r.teach.jog(r.roof, End::Open, true, false, s));
    assertRefused("weatherNotSafe", r.jog(End::Open));
}

// Spec 033: the same for a blind jog (the Hall sensor dead) in the rain.
void test_a_blind_closing_jog_runs_in_the_rain_unparked(void)
{
    Rig r(900.0f);
    r.plant.railCounts = 4095;
    r.steps(200);
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::HallRail);
    r.gates.weather = Safety::Verdict::Unsafe;
    r.gates.parked = false;
    StartInputs s = r.in();
    s.sequenceIdle = false; // can't move: a fault latched
    TEST_ASSERT_TRUE(r.teach.jog(r.roof, End::Closed, true, false, s).accepted);
    for (int i = 0; i < 60; ++i)
    {
        Teach::hold(r.roof, r.roof.jogId());
        r.step();
    }
    TEST_ASSERT_TRUE(r.roof.status().jogging);
    TEST_ASSERT_TRUE(r.roof.status().driving);
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::HallRail);
    r.roof.command(Roof::Command::Stop, Roof::Origin::Person);
    r.steps(100);
    assertRefused("weatherNotSafe", r.jog(End::Open));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_jog_runs_with_the_hall_rail_fault_only);
    RUN_TEST(test_a_jog_may_override_the_end_magnets);
    RUN_TEST(test_a_closing_jog_runs_in_unsafe_weather_daylight_and_unparked);
    RUN_TEST(test_a_blind_closing_jog_runs_in_the_rain_unparked);
    return UNITY_END();
}
