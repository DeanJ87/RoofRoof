// Spec 013: the sun's altitude (SQMeter's reference values) and the daylight
// state: off, unknown, night and day with its band, the texts and the settings.

#include <cmath>
#include <unity.h>

#include "Daylight.h"
#include "SunPosition.h"

using namespace SQM;
using namespace SQM::Daylight;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // London, 2024-06-21: solar noon 12:02 UTC.
    constexpr double LAT = 51.48;
    constexpr double LON = 0.0;
    constexpr int64_t NOON = 1718971320;     // 2024-06-21 12:02 UTC, sun ~62°
    constexpr int64_t MIDNIGHT = 1718928000; // 2024-06-21 00:00 UTC, sun well below

    Settings at(Mode mode, float limit = 0.0f)
    {
        Settings s;
        s.mode = mode;
        s.limitDeg = limit;
        s.locationSet = true;
        s.latitude = LAT;
        s.longitude = LON;
        return s;
    }

    // An epoch near sunrise with the sun at `target` degrees, rising (found by
    // stepping through the morning a second at a time).
    int64_t risingThrough(double target)
    {
        for (int64_t t = MIDNIGHT; t < NOON; ++t)
            if (Astro::sunElevationDeg(t, LAT, LON) >= target)
                return t;
        return NOON;
    }
} // namespace

// SQMeter's test_sun_position, unchanged (heritage: copied from SQMeter 877208b).
void test_sqmeter_reference_values(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.5, 61.96, Astro::sunElevationDeg(1718971320, 51.48, 0.0));
    TEST_ASSERT_FLOAT_WITHIN(0.5, 15.08, Astro::sunElevationDeg(1734782280, 51.48, 0.0));
    TEST_ASSERT_FLOAT_WITHIN(0.5, -61.96, Astro::sunElevationDeg(1734825480, 51.48, 0.0));
    TEST_ASSERT_FLOAT_WITHIN(0.5, 89.5, Astro::sunElevationDeg(1710936420, 0.0, 0.0));
    TEST_ASSERT_FLOAT_WITHIN(0.5, 89.5, Astro::sunElevationDeg(1710936420 + 6 * 3600, 0.0, -90.0));
    TEST_ASSERT_FLOAT_WITHIN(0.5, 79.57, Astro::sunElevationDeg(1734745980, -33.87, 151.21));
}

void test_off_never_closes_or_reports_the_sun(void)
{
    Watch w;
    const View v = w.update(at(Mode::Off), true, NOON);
    TEST_ASSERT_EQUAL(State::Off, v.state);
    TEST_ASSERT_FALSE(v.closes());
    TEST_ASSERT_FALSE(v.on());
    TEST_ASSERT_FALSE(v.known);
    TEST_ASSERT_EQUAL_STRING("Off", describe(v).c_str());
    // Forcing the sun up doesn't turn it on either.
    TEST_ASSERT_EQUAL(State::Off, w.update(at(Mode::Off), true, NOON, true).state);
}

void test_unknown_without_location_or_clock(void)
{
    Watch w;
    Settings s = at(Mode::Close);
    s.locationSet = false;
    View v = w.update(s, true, NOON);
    TEST_ASSERT_EQUAL(State::Unknown, v.state);
    TEST_ASSERT_EQUAL(Missing::Location, v.missing);
    TEST_ASSERT_FALSE(v.closes());
    TEST_ASSERT_FALSE(v.known);
    TEST_ASSERT_EQUAL_STRING("Location not set", describe(v).c_str());
    v = w.update(at(Mode::Close), false, NOON);
    TEST_ASSERT_EQUAL(State::Unknown, v.state);
    TEST_ASSERT_EQUAL(Missing::Clock, v.missing);
    TEST_ASSERT_FALSE(v.closes());
    TEST_ASSERT_EQUAL_STRING("Clock not set", describe(v).c_str());
    // The location is reported first when both are missing.
    v = w.update(s, false, 0);
    TEST_ASSERT_EQUAL(Missing::Location, v.missing);
}

void test_day_and_night(void)
{
    Watch w;
    View v = w.update(at(Mode::Close), true, NOON);
    TEST_ASSERT_EQUAL(State::Day, v.state);
    TEST_ASSERT_TRUE(v.closes());
    TEST_ASSERT_TRUE(v.known);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 61.96, v.altitudeDeg);
    v = w.update(at(Mode::Close), true, MIDNIGHT);
    TEST_ASSERT_EQUAL(State::Night, v.state);
    TEST_ASSERT_FALSE(v.closes());
}

void test_only_alert_and_close_closes(void)
{
    Watch w;
    TEST_ASSERT_FALSE(w.update(at(Mode::Alert), true, NOON).closes());
    TEST_ASSERT_TRUE(w.update(at(Mode::Alert), true, NOON).on());
    TEST_ASSERT_TRUE(w.update(at(Mode::Close), true, NOON).closes());
    TEST_ASSERT_FALSE(w.update(at(Mode::Off), true, NOON).closes());
}

// FR-004: day at or above the threshold, night again only below it minus 0.5°.
void test_threshold_and_band_edges(void)
{
    const float limit = 2.0f;
    const int64_t below = risingThrough(limit) - 1;
    const int64_t atLimit = risingThrough(limit);
    TEST_ASSERT_TRUE(Astro::sunElevationDeg(below, LAT, LON) < limit);
    TEST_ASSERT_TRUE(Astro::sunElevationDeg(atLimit, LAT, LON) >= limit);
    Watch w;
    TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, limit), true, below).state);
    TEST_ASSERT_EQUAL(State::Day, w.update(at(Mode::Close, limit), true, atLimit).state);
    // Back below the threshold but within the band: still day.
    TEST_ASSERT_EQUAL(State::Day, w.update(at(Mode::Close, limit), true, below).state);
    const int64_t bandEdge = risingThrough(limit - BAND_DEG);
    TEST_ASSERT_EQUAL(State::Day, w.update(at(Mode::Close, limit), true, bandEdge).state);
    // Just below the band: night.
    TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, limit), true, bandEdge - 1).state);
    // And within the band again from night: still night (the band only holds day).
    TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, limit), true, bandEdge).state);
    TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, limit), true, below).state);
}

void test_band_resets_on_off_or_unknown(void)
{
    const float limit = 2.0f;
    const int64_t inBand = risingThrough(limit - 0.25);
    for (int unknownOrOff = 0; unknownOrOff < 3; ++unknownOrOff)
    {
        Watch w;
        TEST_ASSERT_EQUAL(State::Day, w.update(at(Mode::Close, limit), true, NOON).state);
        Settings broken = at(Mode::Close, limit);
        if (unknownOrOff == 0)
            broken.mode = Mode::Off;
        else if (unknownOrOff == 1)
            broken.locationSet = false;
        w.update(broken, unknownOrOff != 2, NOON);
        // The next known altitude decides from scratch: in the band is night.
        TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, limit), true, inBand).state);
    }
}

// FR-004: a mode or threshold change decides from scratch (the band doesn't carry over).
void test_band_resets_on_mode_or_threshold_change(void)
{
    const float limit = 2.0f;
    const int64_t inBand = risingThrough(limit - 0.25);
    Watch w;
    TEST_ASSERT_EQUAL(State::Day, w.update(at(Mode::Alert, limit), true, NOON).state);
    TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, limit), true, inBand).state);
    Watch v;
    TEST_ASSERT_EQUAL(State::Day, v.update(at(Mode::Close, limit), true, NOON).state);
    TEST_ASSERT_EQUAL(State::Night, v.update(at(Mode::Close, limit - 0.1f), true, risingThrough(limit - 0.35)).state);
}

void test_threshold_change_applies_at_once(void)
{
    Watch w;
    TEST_ASSERT_EQUAL(State::Day, w.update(at(Mode::Close, 0.0f), true, NOON).state);
    TEST_ASSERT_EQUAL(State::Night, w.update(at(Mode::Close, 30.0f), true, risingThrough(10.0)).state);
}

void test_forced_sun_up(void)
{
    Watch w;
    Settings s = at(Mode::Close, 5.0f);
    s.locationSet = false;
    const View v = w.update(s, false, 0, true);
    TEST_ASSERT_EQUAL(State::Day, v.state);
    TEST_ASSERT_TRUE(v.closes());
    TEST_ASSERT_TRUE(v.known);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, static_cast<float>(v.altitudeDeg));
    TEST_ASSERT_EQUAL_STRING("Sun 15.0° ≥ 5.0°", describe(v).c_str());
    TEST_ASSERT_EQUAL(State::Day, w.view().state);
}

// FR-005: the value and the limit.
void test_texts(void)
{
    View v;
    v.mode = Mode::Close;
    v.known = true;
    v.limitDeg = 0.0f;
    v.state = State::Day;
    v.altitudeDeg = 3.24;
    TEST_ASSERT_EQUAL_STRING("Sun 3.2° ≥ 0.0°", describe(v).c_str());
    v.altitudeDeg = -0.2;
    TEST_ASSERT_EQUAL_STRING("Sun -0.2°, day until < -0.5°", describe(v).c_str());
    v.state = State::Night;
    v.altitudeDeg = -4.1;
    TEST_ASSERT_EQUAL_STRING("Sun -4.1° < 0.0°", describe(v).c_str());
    TEST_ASSERT_EQUAL_STRING("The mount is being parked and the roof closed.", actionText(Mode::Close, true).c_str());
    TEST_ASSERT_EQUAL_STRING("Daylight is set to alert only, so nothing closes it.", actionText(Mode::Alert, false).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "The automatic close can't close it now: see the roof's other alerts.", actionText(Mode::Close, false).c_str());
}

void test_names_and_parse(void)
{
    TEST_ASSERT_EQUAL_STRING("off", modeName(Mode::Off));
    TEST_ASSERT_EQUAL_STRING("alert", modeName(Mode::Alert));
    TEST_ASSERT_EQUAL_STRING("close", modeName(Mode::Close));
    TEST_ASSERT_EQUAL_STRING("off", stateName(State::Off));
    TEST_ASSERT_EQUAL_STRING("unknown", stateName(State::Unknown));
    TEST_ASSERT_EQUAL_STRING("night", stateName(State::Night));
    TEST_ASSERT_EQUAL_STRING("day", stateName(State::Day));
    Mode m = Mode::Off;
    TEST_ASSERT_TRUE(parseMode("close", m));
    TEST_ASSERT_EQUAL(Mode::Close, m);
    TEST_ASSERT_FALSE(parseMode("Close", m));
    TEST_ASSERT_EQUAL(Mode::Close, m);
}

void test_settings_from_config(void)
{
    Config c = Config::createDefault();
    Settings s = settingsFrom(c);
    TEST_ASSERT_EQUAL(Mode::Alert, s.mode); // FR-001: the default is alert
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s.limitDeg);
    TEST_ASSERT_FALSE(s.locationSet);
    c.daylight.mode = DaylightConfig::Mode::Close;
    c.daylight.sunAltitudeDeg = -6.0f;
    c.location = {true, 51.5, -0.1};
    s = settingsFrom(c);
    TEST_ASSERT_EQUAL(Mode::Close, s.mode);
    TEST_ASSERT_EQUAL_FLOAT(-6.0f, s.limitDeg);
    TEST_ASSERT_TRUE(s.locationSet);
    TEST_ASSERT_TRUE(s.latitude == 51.5);
    TEST_ASSERT_TRUE(s.longitude == -0.1);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sqmeter_reference_values);
    RUN_TEST(test_off_never_closes_or_reports_the_sun);
    RUN_TEST(test_unknown_without_location_or_clock);
    RUN_TEST(test_day_and_night);
    RUN_TEST(test_only_alert_and_close_closes);
    RUN_TEST(test_threshold_and_band_edges);
    RUN_TEST(test_band_resets_on_off_or_unknown);
    RUN_TEST(test_band_resets_on_mode_or_threshold_change);
    RUN_TEST(test_threshold_change_applies_at_once);
    RUN_TEST(test_forced_sun_up);
    RUN_TEST(test_texts);
    RUN_TEST(test_names_and_parse);
    RUN_TEST(test_settings_from_config);
    return UNITY_END();
}
