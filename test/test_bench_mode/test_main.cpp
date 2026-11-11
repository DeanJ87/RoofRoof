// Bench mode's pure decisions (spec 011 FR-021, FR-023): the supply guard that
// stops bench firmware simulating a roof on a powered board, and the pin rule
// together with the plant (the plant only moves on what the logic decided,
// while the pins stay low).

#include <cmath>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unity.h>

#include "MotionGuards.h"
#include "RoofController.h"

#include "BenchGuard.h"
#include "DriverLogic.h"
#include "RoofPlant.h"

using namespace SQM;
using SQM::RoofPlant::BenchSupplyGuard;

void setUp(void) {}
void tearDown(void) {}

void test_bare_board_never_trips(void)
{
    BenchSupplyGuard guard;
    for (uint32_t t = 0; t < 600000; t += 20)
        TEST_ASSERT_FALSE(guard.update(0.4f, t));
    TEST_ASSERT_FALSE(guard.update(5.99f, 600020));
    TEST_ASSERT_FALSE(guard.tripped());
}

// The limit is >=: 6.0 V counts. One second without a break trips it.
void test_supply_at_the_limit_for_a_second_trips(void)
{
    BenchSupplyGuard guard;
    TEST_ASSERT_FALSE(guard.update(6.0f, 1000));
    TEST_ASSERT_FALSE(guard.update(6.0f, 1999));
    TEST_ASSERT_TRUE(guard.update(6.0f, 2000));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, guard.measured());
}

void test_a_break_restarts_the_second(void)
{
    BenchSupplyGuard guard;
    guard.update(12.4f, 0);
    guard.update(12.4f, 900);
    TEST_ASSERT_FALSE(guard.update(1.0f, 920)); // a clean reading breaks the run
    TEST_ASSERT_FALSE(guard.update(12.4f, 940));
    TEST_ASSERT_FALSE(guard.update(12.4f, 1900));
    TEST_ASSERT_TRUE(guard.update(12.4f, 1940));
}

// Doubt is a powered roof: no reading counts like 12 V.
void test_unreadable_counts_as_powered(void)
{
    BenchSupplyGuard guard;
    guard.update(NAN, 0);
    TEST_ASSERT_TRUE(guard.update(INFINITY, 1000));
    TEST_ASSERT_TRUE(std::isinf(guard.measured()));
    TEST_ASSERT_TRUE(RoofPlant::supplyPresent(NAN));
    TEST_ASSERT_FALSE(RoofPlant::supplyPresent(0.0f));
}

// Once tripped, it stays tripped for the boot, whatever it reads later.
void test_trip_is_latched(void)
{
    BenchSupplyGuard guard;
    guard.update(12.4f, 0);
    TEST_ASSERT_TRUE(guard.update(12.4f, 1000));
    for (uint32_t t = 1020; t < 100000; t += 20)
        TEST_ASSERT_TRUE(guard.update(0.0f, t));
    TEST_ASSERT_EQUAL_FLOAT(12.4f, guard.measured());
}

void test_confirm_survives_a_millis_wrap(void)
{
    BenchSupplyGuard guard;
    guard.update(12.0f, 0xFFFFFF00u);
    TEST_ASSERT_FALSE(guard.update(12.0f, 0x00000100u)); // 512 ms later
    TEST_ASSERT_TRUE(guard.update(12.0f, 0x00000300u));  // 1024 ms later
}

// In a bench build the plant follows the logical levels while the pins stay low.
void test_plant_moves_on_logic_while_pins_stay_low(void)
{
    RoofPlant::Plant plant = RoofPlant::forSettings(Roof::Settings(), 900.0f);
    const Driver::Outputs logical = Driver::outputsFor(Driver::Command::Forward, 255);
    const Driver::Outputs pins = Driver::physicalOutputs(logical, true);
    TEST_ASSERT_FALSE(pins.enable);
    TEST_ASSERT_TRUE(plant.drive(RoofPlant::outputOf(logical), 1000) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plant.drive(RoofPlant::outputOf(pins), 1000));
}

namespace
{
    // The bench build's control step, end to end: the logic's output through
    // the driver levels (outputsFor), the plant (benchStep), the sense
    // scaling back (runningAmps), the logic.
    struct BenchRig
    {
        Roof::Controller roof;
        RoofPlant::Plant plant;
        BenchSupplyGuard guard;
        uint32_t nowMs = 1000;
        float realVolts = 0.2f;
        bool currentSense = false;
        int latches = 0;

        explicit BenchRig(const Roof::Settings &settings, float startMm)
            : roof(settings),
              plant(RoofPlant::forSettings(settings, startMm))
        {
            currentSense = settings.currentSenseEnabled;
        }

        void step(uint32_t gapMs = 20)
        {
            nowMs += gapMs;
            RoofPlant::BenchStepIn in;
            in.real.nowMs = nowMs;
            in.real.hallCounts = 4095; // the floating Hall input: never used
            in.real.supplyVolts = realVolts;
            in.gapMs = gapMs;
            const Roof::Output out = roof.output();
            in.logical = Driver::outputsFor(out.command, out.duty);
            in.currentSense = currentSense;
            const RoofPlant::BenchStepOut result = RoofPlant::benchStep(guard, plant, in);
            if (result.latch)
            {
                ++latches;
                roof.raiseFault({Roof::FaultKind::External, result.measured, BenchSupplyGuard::LIMIT_VOLTS});
            }
            Roof::Readings readings = result.readings;
            readings.motorAmps = RoofIo::runningAmps(readings.motorAmps, roof.status(), roof.output(), 0.0f);
            roof.tick(readings);
        }

        void steps(int count)
        {
            for (int i = 0; i < count; ++i)
                step();
        }
    };
} // namespace

// The whole pipeline opens and closes the roof, with current sense on and off.
void test_bench_pipeline_opens_and_closes(void)
{
    for (bool sense : {false, true})
    {
        Roof::Settings settings;
        settings.currentSenseEnabled = sense;
        BenchRig rig(settings, 0.0f);
        rig.steps(40);
        TEST_ASSERT_EQUAL(Roof::State::Closed, rig.roof.status().state);
        TEST_ASSERT_TRUE(rig.roof.command(Roof::Command::Open, Roof::Origin::Person).accepted);
        rig.steps(5000);
        TEST_ASSERT_EQUAL(Roof::State::Open, rig.roof.status().state);
        TEST_ASSERT_TRUE(rig.roof.command(Roof::Command::Close, Roof::Origin::Person).accepted);
        rig.steps(5000);
        TEST_ASSERT_EQUAL(Roof::State::Closed, rig.roof.status().state);
        TEST_ASSERT_EQUAL(Roof::FaultKind::None, rig.roof.status().fault.kind);
        TEST_ASSERT_EQUAL(0, rig.latches);
    }
}

// The guard sees the real supply before anything is replaced; after a trip
// every step latches again, so a cleared fault comes straight back.
void test_bench_step_latches_on_a_powered_board_every_step(void)
{
    BenchRig rig(Roof::Settings(), 0.0f);
    rig.realVolts = 12.4f;
    rig.steps(49);
    TEST_ASSERT_EQUAL(0, rig.latches);
    rig.steps(2);
    TEST_ASSERT_TRUE(rig.latches > 0);
    TEST_ASSERT_EQUAL(Roof::State::Error, rig.roof.status().state);
    rig.realVolts = 0.0f; // latched for the boot
    rig.roof.clearFault(Roof::Origin::Person);
    const int before = rig.latches;
    rig.step();
    TEST_ASSERT_EQUAL(before + 1, rig.latches);
    TEST_ASSERT_EQUAL(Roof::State::Error, rig.roof.status().state);
}

// A late step moves the roof at most MAX_PLANT_STEP_MS worth.
void test_bench_step_clamps_a_long_gap(void)
{
    Roof::Settings settings;
    settings.currentSenseEnabled = true;
    RoofPlant::Plant plant = RoofPlant::forSettings(settings, 900.0f);
    BenchSupplyGuard guard;
    RoofPlant::BenchStepIn in;
    in.real.nowMs = 5000;
    in.gapMs = 10000;
    in.logical = Driver::outputsFor(Driver::Command::Forward, 255);
    RoofPlant::benchStep(guard, plant, in);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 900.0f + 38.0f * 0.1f, plant.positionMm);
    // Current sense off: no current reading, whatever the plant draws.
    TEST_ASSERT_TRUE(std::isnan(RoofPlant::benchStep(guard, plant, in).readings.motorAmps));
    in.currentSense = true;
    TEST_ASSERT_TRUE(RoofPlant::benchStep(guard, plant, in).readings.motorAmps > 0.0f);
}

// Constitution I: only MotorDriver.cpp writes pins, so the bench rule
// (physicalOutputs) covers every write. A new pin write elsewhere fails here.
void test_only_the_motor_driver_writes_output_pins(void)
{
    DIR *dir = opendir("src");
    TEST_ASSERT_NOT_NULL_MESSAGE(dir, "run from the project root");
    const char *const WRITES[] = {"gpio_set_level", "ledcWrite", "ledcAttach", "digitalWrite", "analogWrite", "dacWrite", "sigmaDelta"};
    int scanned = 0;
    for (dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name.size() < 4 || (name.substr(name.size() - 4) != ".cpp" && name.substr(name.size() - 2) != ".h"))
            continue;
        std::ifstream in("src/" + name);
        std::stringstream text;
        text << in.rdbuf();
        ++scanned;
        if (name == "MotorDriver.cpp")
            continue;
        for (const char *write : WRITES)
            TEST_ASSERT_TRUE_MESSAGE(text.str().find(write) == std::string::npos, (name + " writes a pin: " + write).c_str());
    }
    closedir(dir);
    TEST_ASSERT_TRUE(scanned > 20);
    // In MotorDriver.cpp, EN and the PWM are written only through the guarded helpers.
    std::ifstream in("src/MotorDriver.cpp");
    std::stringstream text;
    text << in.rdbuf();
    const std::string source = text.str();
    TEST_ASSERT_TRUE(source.find("Driver::physicalOutputs(logical, Bench::BUILD)") != std::string::npos);
    TEST_ASSERT_TRUE(source.find("if (pwmReady && !Bench::BUILD)") != std::string::npos);
    size_t count = 0;
    for (size_t at = source.find("ledcWrite("); at != std::string::npos; at = source.find("ledcWrite(", at + 1))
        ++count;
    TEST_ASSERT_EQUAL(1, count);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bare_board_never_trips);
    RUN_TEST(test_supply_at_the_limit_for_a_second_trips);
    RUN_TEST(test_a_break_restarts_the_second);
    RUN_TEST(test_unreadable_counts_as_powered);
    RUN_TEST(test_trip_is_latched);
    RUN_TEST(test_confirm_survives_a_millis_wrap);
    RUN_TEST(test_plant_moves_on_logic_while_pins_stay_low);
    RUN_TEST(test_bench_pipeline_opens_and_closes);
    RUN_TEST(test_bench_step_latches_on_a_powered_board_every_step);
    RUN_TEST(test_bench_step_clamps_a_long_gap);
    RUN_TEST(test_only_the_motor_driver_writes_output_pins);
    return UNITY_END();
}
