// The simulated roof (spec 011 FR-001 to FR-003; lib/RoofPlantLogic): set up
// from the roof settings, it reads every magnet past its threshold and never
// at a rail, follows the open direction and polarity, sags under load, stalls
// at a stop or a jam, and takes the motion logic through a full open and close.

#include <cmath>
#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "HallFilter.h"
#include "RoofPlant.h"
#include "RoofSettings.h"

using namespace SQM;
using namespace SQM::Roof;
using DriveCommand = SQM::Driver::Command;
using SQM::RoofPlant::Plant;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Output drive(DriveCommand command, uint8_t duty)
    {
        Output out;
        out.command = command;
        out.duty = duty;
        return out;
    }

    // Steps a controller and a plant set up from `settings`, from `startMm`.
    struct Rig
    {
        Controller roof;
        Plant plant;
        uint32_t nowMs = 1000;

        Rig(const Settings &settings, float startMm)
            : roof(settings),
              plant(RoofPlant::forSettings(settings, startMm))
        {
            steps(40); // the Hall settles and the start-up dead time passes
        }

        void step()
        {
            plant.drive(roof.output(), 20);
            nowMs += 20;
            roof.tick(plant.readings(nowMs));
        }

        void steps(int count)
        {
            for (int i = 0; i < count; ++i)
                step();
        }

        // Steps until the roof rests; returns the ms it took.
        uint32_t runToRest(uint32_t maxMs)
        {
            const uint32_t start = nowMs;
            while (nowMs - start < maxMs)
            {
                step();
                const State s = roof.status().state;
                if (s != State::Opening && s != State::Closing)
                    break;
            }
            return nowMs - start;
        }
    };
} // namespace

void test_counts_lie_past_each_threshold_and_off_the_rails(void)
{
    Settings settings;
    const Plant plant = RoofPlant::forSettings(settings, 0.0f);
    TEST_ASSERT_TRUE(plant.lowMagnetCounts < settings.hallLowCounts - HALL_HYSTERESIS_COUNTS);
    TEST_ASSERT_TRUE(plant.lowMagnetCounts > HALL_RAIL_LOW_COUNTS);
    TEST_ASSERT_TRUE(plant.highMagnetCounts > settings.hallHighCounts + HALL_HYSTERESIS_COUNTS);
    TEST_ASSERT_TRUE(plant.highMagnetCounts < HALL_RAIL_HIGH_COUNTS);
    TEST_ASSERT_TRUE(plant.restingCounts > settings.hallLowCounts && plant.restingCounts < settings.hallHighCounts);
    // The same for thresholds near the rails.
    settings.hallLowCounts = 100;
    settings.hallHighCounts = 4000;
    const Plant edge = RoofPlant::forSettings(settings, 0.0f);
    TEST_ASSERT_TRUE(edge.lowMagnetCounts < 100 && edge.lowMagnetCounts > HALL_RAIL_LOW_COUNTS);
    TEST_ASSERT_TRUE(edge.highMagnetCounts > 4000 && edge.highMagnetCounts < HALL_RAIL_HIGH_COUNTS);
}

void test_polarity_direction_and_current_follow_the_settings(void)
{
    Settings settings;
    settings.hallHighEnd = End::Closed;
    settings.openDirection = DriveCommand::Reverse;
    settings.currentSenseEnabled = true;
    settings.maxCurrentAmps = 8.0f;
    Plant plant = RoofPlant::forSettings(settings, 0.0f);
    TEST_ASSERT_TRUE(plant.highEndClosed);
    TEST_ASSERT_EQUAL(plant.highMagnetCounts, plant.hallCounts()); // closed end reads high
    TEST_ASSERT_TRUE(plant.currentFitted);
    TEST_ASSERT_TRUE(plant.drive(drive(DriveCommand::Reverse, 255), 1000) > 0.0f); // Reverse opens
    TEST_ASSERT_EQUAL_FLOAT(2.4f, plant.motorAmps());
    plant.jammed = true;
    plant.drive(drive(DriveCommand::Reverse, 255), 1000);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, plant.motorAmps()); // stall: over the 8 A limit
    plant.jammed = false;
    plant.positionMm = 500.0f;
    plant.drive(drive(DriveCommand::Reverse, 102), 20); // 40% duty
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.4f * 0.4f, plant.senseAmps());
    plant.drive(drive(DriveCommand::Off, 0), 20);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plant.senseAmps());
    plant.jammed = true;
    settings.currentSenseEnabled = false;
    RoofPlant::applySettings(plant, settings);
    TEST_ASSERT_TRUE(std::isnan(plant.senseAmps()));
    TEST_ASSERT_TRUE(std::isnan(plant.motorAmps()));
    TEST_ASSERT_TRUE(plant.jammed); // injected faults are kept
}

void test_speed_scales_with_duty_and_stops_hold(void)
{
    Settings settings;
    settings.travelMm = 1000;
    settings.speedMmPerSecond = 50.0f;
    Plant plant = RoofPlant::forSettings(settings, 500.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, plant.drive(drive(DriveCommand::Forward, 255), 1000));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 25.0f, plant.drive(drive(DriveCommand::Forward, 128), 1000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plant.drive(drive(DriveCommand::Off, 255), 1000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plant.drive(drive(DriveCommand::Forward, 0), 1000));
    plant.drive(drive(DriveCommand::Forward, 255), 60000);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, plant.positionMm); // the open stop
    plant.drive(drive(DriveCommand::Reverse, 255), 60000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plant.positionMm); // the closed stop
}

void test_supply_sags_only_while_driving(void)
{
    Plant plant = RoofPlant::forSettings(Settings(), 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(12.6f, plant.supplyNow());
    plant.drive(drive(DriveCommand::Forward, 255), 20);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.3f, plant.supplyNow());
    plant.drive(drive(DriveCommand::Off, 0), 20);
    TEST_ASSERT_EQUAL_FLOAT(12.6f, plant.supplyNow());
    plant.supplyVolts = 10.5f;
    TEST_ASSERT_EQUAL_FLOAT(10.5f, plant.readings(5).supplyVolts);
    // The test plant keeps the old behaviour: no sag unless asked.
    Plant raw;
    raw.drive(drive(DriveCommand::Forward, 255), 20);
    TEST_ASSERT_EQUAL_FLOAT(12.6f, raw.supplyNow());
}

void test_output_of_pin_levels(void)
{
    Driver::Outputs levels;
    TEST_ASSERT_EQUAL(DriveCommand::Off, RoofPlant::outputOf(levels).command);
    levels.rpwm = 200; // EN low: nothing moves
    TEST_ASSERT_EQUAL(DriveCommand::Off, RoofPlant::outputOf(levels).command);
    levels.enable = true;
    TEST_ASSERT_EQUAL(DriveCommand::Forward, RoofPlant::outputOf(levels).command);
    TEST_ASSERT_EQUAL_UINT8(200, RoofPlant::outputOf(levels).duty);
    levels.lpwm = 10; // both: never a drive
    TEST_ASSERT_EQUAL(DriveCommand::Off, RoofPlant::outputOf(levels).command);
    levels.rpwm = 0;
    TEST_ASSERT_EQUAL(DriveCommand::Reverse, RoofPlant::outputOf(levels).command);
    TEST_ASSERT_EQUAL_UINT8(10, RoofPlant::outputOf(levels).duty);
}

// The plant takes the real motion logic from closed to open and back, in
// about the travel time the settings give (SC-001), for the defaults and for
// reversed polarity and direction.
void test_full_open_and_close_with_the_motion_logic(void)
{
    Settings defaults;
    Settings odd;
    odd.travelMm = 600;
    odd.speedMmPerSecond = 60.0f;
    odd.hallHighEnd = End::Closed;
    odd.openDirection = DriveCommand::Reverse;
    odd.hallLowCounts = 900;
    odd.hallHighCounts = 3300;
    for (const Settings &settings : {defaults, odd})
    {
        Rig rig(settings, 0.0f);
        TEST_ASSERT_EQUAL(State::Closed, rig.roof.status().state);
        TEST_ASSERT_TRUE(rig.roof.command(Roof::Command::Open, Origin::Person).accepted);
        const uint32_t openMs = rig.runToRest(400000);
        TEST_ASSERT_EQUAL(State::Open, rig.roof.status().state);
        const float nominalMs = 1000.0f * static_cast<float>(settings.travelMm) / settings.speedMmPerSecond;
        TEST_ASSERT_TRUE(openMs > nominalMs * 0.9f);
        TEST_ASSERT_TRUE(openMs < nominalMs * 1.6f); // the ramps and the slow ends add time
        TEST_ASSERT_TRUE(rig.roof.command(Roof::Command::Close, Origin::Person).accepted);
        rig.runToRest(400000);
        TEST_ASSERT_EQUAL(State::Closed, rig.roof.status().state);
        TEST_ASSERT_EQUAL(FaultKind::None, rig.roof.status().fault.kind);
    }
}

// A jam with current sense on is an over-current fault; without, the move times out.
void test_jam_faults_the_move(void)
{
    Settings settings;
    settings.currentSenseEnabled = true;
    Rig withCurrent(settings, 0.0f);
    withCurrent.plant.jammed = true;
    withCurrent.roof.command(Roof::Command::Open, Origin::Person);
    withCurrent.runToRest(400000);
    TEST_ASSERT_EQUAL(State::Error, withCurrent.roof.status().state);
    TEST_ASSERT_EQUAL(FaultKind::OverCurrent, withCurrent.roof.status().fault.kind);

    Rig noCurrent(Settings(), 0.0f);
    noCurrent.plant.jammed = true;
    noCurrent.roof.command(Roof::Command::Open, Origin::Person);
    noCurrent.runToRest(400000);
    TEST_ASSERT_EQUAL(State::Error, noCurrent.roof.status().state);
    TEST_ASSERT_EQUAL(FaultKind::DidNotLeave, noCurrent.roof.status().fault.kind);
}

// A low supply refuses a move and stops one under way.
void test_low_supply_refuses_and_stops(void)
{
    Rig rig(Settings(), 0.0f);
    rig.plant.supplyVolts = 10.5f;
    rig.steps(2);
    const CommandResult refused = rig.roof.command(Roof::Command::Open, Origin::Person);
    TEST_ASSERT_FALSE(refused.accepted);
    TEST_ASSERT_EQUAL(Refusal::SupplyLow, refused.reason);
    rig.plant.supplyVolts = 12.6f;
    rig.steps(2);
    TEST_ASSERT_TRUE(rig.roof.command(Roof::Command::Open, Origin::Person).accepted);
    rig.steps(200);
    rig.plant.supplyVolts = 10.5f;
    rig.steps(50);
    TEST_ASSERT_EQUAL(State::Error, rig.roof.status().state);
    TEST_ASSERT_EQUAL(FaultKind::SupplyLow, rig.roof.status().fault.kind);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_counts_lie_past_each_threshold_and_off_the_rails);
    RUN_TEST(test_polarity_direction_and_current_follow_the_settings);
    RUN_TEST(test_speed_scales_with_duty_and_stops_hold);
    RUN_TEST(test_supply_sags_only_while_driving);
    RUN_TEST(test_output_of_pin_levels);
    RUN_TEST(test_full_open_and_close_with_the_motion_logic);
    RUN_TEST(test_jam_faults_the_move);
    RUN_TEST(test_low_supply_refuses_and_stops);
    return UNITY_END();
}
