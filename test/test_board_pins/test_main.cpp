// The pin rules and the boot decision (spec 023 FR-003 to FR-006, FR-010,
// FR-011, research R2, R3, R5; constitution I: an invalid set drives nothing).

#include <cstring>
#include <unity.h>

#include "PinMap.h"
#include "PinRules.h"

using namespace SQM::Board;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const Facts DEVKIT = facts(BoardId::Esp32Devkit, false);
    const Facts DEVKIT_PSRAM = facts(BoardId::Esp32Devkit, true);
    const Facts WT32 = facts(BoardId::Wt32Eth01, false);

    PinMap custom(BoardId board = BoardId::Esp32Devkit)
    {
        PinMap map = board == BoardId::Wt32Eth01 ? presetMap(Preset::Wt32CarrierV1) : presetMap(Preset::Devkit);
        map.preset = Preset::Custom;
        return map;
    }

    int countErrors(const PinReport &report)
    {
        int n = 0;
        for (const Issue &issue : report.issues)
            n += issue.error ? 1 : 0;
        return n;
    }

    // The one error the report has, checked for its function, pin and code.
    void assertOnlyError(const PinReport &report, Function function, uint8_t pin, IssueCode code)
    {
        TEST_ASSERT_EQUAL_INT(1, countErrors(report));
        const Issue *error = report.firstError();
        TEST_ASSERT_NOT_NULL(error);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(function), static_cast<uint8_t>(error->function));
        TEST_ASSERT_EQUAL_UINT8(pin, error->pin);
        TEST_ASSERT_EQUAL_STRING(issueCodeName(code), issueCodeName(error->code));
        TEST_ASSERT_TRUE(!error->text.empty());
    }

    PinReport withPin(Function function, uint8_t pin, const Facts &f = DEVKIT, BoardId board = BoardId::Esp32Devkit)
    {
        PinMap map = custom(board);
        map.setPin(function, pin);
        return check(map, f);
    }

    bool hasWarning(const PinReport &report, Function function, IssueCode code)
    {
        for (const Issue &issue : report.issues)
            if (!issue.error && issue.function == function && issue.code == code)
                return true;
        return false;
    }
} // namespace

// --- Presets ---------------------------------------------------------------

void test_devkit_preset_is_the_old_map_and_the_default(void)
{
    const PinMap map = presetMap(Preset::Devkit);
    const PinMap byDefault;
    TEST_ASSERT_TRUE(map == byDefault);
    TEST_ASSERT_EQUAL_UINT8(25, map.rpwm);
    TEST_ASSERT_EQUAL_UINT8(26, map.lpwm);
    TEST_ASSERT_EQUAL_UINT8(27, map.enable);
    TEST_ASSERT_EQUAL_UINT8(34, map.currentR);
    TEST_ASSERT_EQUAL_UINT8(35, map.currentL);
    TEST_ASSERT_EQUAL_UINT8(36, map.supply);
    TEST_ASSERT_EQUAL_UINT8(39, map.hall);
    TEST_ASSERT_EQUAL_UINT8(32, map.parkA);
    TEST_ASSERT_EQUAL_UINT8(33, map.parkB);
    const PinReport report = check(map, DEVKIT);
    TEST_ASSERT_TRUE(report.valid());
    TEST_ASSERT_EQUAL_size_t(0, report.issues.size()); // no strap at all
}

void test_wt32_carrier_preset_and_its_two_warnings(void)
{
    const PinMap map = presetMap(Preset::Wt32CarrierV1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(BoardId::Wt32Eth01), static_cast<uint8_t>(map.board));
    const uint8_t expected[] = {14, 15, 4, 35, 32, 36, 39, 33, 5};
    for (size_t i = 0; i < FUNCTION_COUNT; ++i)
        TEST_ASSERT_EQUAL_UINT8(expected[i], map.pin(static_cast<Function>(i)));
    const PinReport report = check(map, WT32);
    TEST_ASSERT_TRUE(report.valid());
    TEST_ASSERT_EQUAL_size_t(2, report.issues.size());
    TEST_ASSERT_TRUE(hasWarning(report, Function::Lpwm, IssueCode::Strap15));
    TEST_ASSERT_TRUE(hasWarning(report, Function::ParkB, IssueCode::Strap5));
}

void test_names_round_trip(void)
{
    for (Preset p : {Preset::Devkit, Preset::Wt32CarrierV1, Preset::Custom, Preset::Unset})
    {
        Preset back = Preset::Devkit;
        TEST_ASSERT_TRUE(parsePreset(presetName(p), back));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(p), static_cast<uint8_t>(back));
    }
    for (BoardId b : {BoardId::Esp32Devkit, BoardId::Wt32Eth01})
    {
        BoardId back = BoardId::Esp32Devkit;
        TEST_ASSERT_TRUE(parseBoard(boardName(b), back));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(b), static_cast<uint8_t>(back));
    }
    Preset p = Preset::Devkit;
    BoardId b = BoardId::Esp32Devkit;
    TEST_ASSERT_FALSE(parsePreset("wt32", p));
    TEST_ASSERT_FALSE(parseBoard("esp32", b));
    TEST_ASSERT_EQUAL_STRING("enable", functionName(Function::Enable));
    TEST_ASSERT_EQUAL_STRING("EN", functionLabel(Function::Enable));
}

// --- Errors: one per rule --------------------------------------------------

void test_missing_gpio(void)
{
    assertOnlyError(withPin(Function::ParkA, 20), Function::ParkA, 20, IssueCode::Missing);
    assertOnlyError(withPin(Function::ParkA, 24), Function::ParkA, 24, IssueCode::Missing);
    assertOnlyError(withPin(Function::ParkA, 40), Function::ParkA, 40, IssueCode::Missing);
    assertOnlyError(withPin(Function::ParkA, 255), Function::ParkA, 255, IssueCode::Missing);
}

void test_gpio_37_and_38_are_not_brought_out(void)
{
    assertOnlyError(withPin(Function::Hall, 37), Function::Hall, 37, IssueCode::Missing);
    assertOnlyError(withPin(Function::Supply, 38), Function::Supply, 38, IssueCode::Missing);
}

// The boot probe drives them on a device that doesn't know its wiring yet.
void test_probe_pins_on_every_board(void)
{
    for (uint8_t pin : {0, 18, 23}) // 16 reads as psram first
    {
        assertOnlyError(withPin(Function::Enable, pin), Function::Enable, pin, IssueCode::Probe);
        assertOnlyError(withPin(Function::ParkA, pin), Function::ParkA, pin, IssueCode::Probe);
    }
}

void test_flash_pins(void)
{
    for (uint8_t pin = 6; pin <= 11; ++pin)
        assertOnlyError(withPin(Function::Rpwm, pin), Function::Rpwm, pin, IssueCode::Flash);
}

// This build can't detect a WROVER's PSRAM, so 16 and 17 are reserved everywhere.
void test_psram_pins_on_every_board(void)
{
    for (const Facts *f : {&DEVKIT, &DEVKIT_PSRAM})
    {
        assertOnlyError(withPin(Function::Rpwm, 16, *f), Function::Rpwm, 16, IssueCode::Psram);
        assertOnlyError(withPin(Function::Rpwm, 17, *f), Function::Rpwm, 17, IssueCode::Psram);
    }
    assertOnlyError(withPin(Function::Rpwm, 17, WT32, BoardId::Wt32Eth01), Function::Rpwm, 17, IssueCode::Psram);
}

// FR-008: where a LAN8720 answered, only the WT32-ETH01 board.
void test_a_detected_wt32_refuses_another_board(void)
{
    Facts seen = DEVKIT;
    seen.requireWt32 = true;
    const PinReport report = check(presetMap(Preset::Devkit), seen);
    TEST_ASSERT_FALSE(report.valid());
    TEST_ASSERT_EQUAL_STRING("boardDetected", issueCodeName(report.firstError()->code));
    TEST_ASSERT_EQUAL_STRING(
        "This board is a WT32-ETH01 (its Ethernet answered): choose a WT32-ETH01 board", report.firstError()->text.c_str());
    Facts wt32Seen = WT32;
    wt32Seen.requireWt32 = true;
    TEST_ASSERT_TRUE(check(presetMap(Preset::Wt32CarrierV1), wt32Seen).valid());
    TEST_ASSERT_EQUAL_size_t(0, check(presetMap(Preset::Unset), seen).issues.size());
}

void test_uart0(void)
{
    assertOnlyError(withPin(Function::ParkB, 1), Function::ParkB, 1, IssueCode::Uart);
    assertOnlyError(withPin(Function::ParkB, 3), Function::ParkB, 3, IssueCode::Uart);
}

void test_wt32_ethernet_pins(void)
{
    for (uint8_t pin : {0, 16, 18, 19, 21, 22, 23, 25, 26, 27})
        assertOnlyError(withPin(Function::Rpwm, pin, WT32, BoardId::Wt32Eth01), Function::Rpwm, pin, IssueCode::Ethernet);
    // The same pins are fine on a DevKit (no Ethernet), 0 only with its warning.
    TEST_ASSERT_TRUE(withPin(Function::Rpwm, 19).valid());
}

void test_wt32_header(void)
{
    assertOnlyError(withPin(Function::Rpwm, 13, WT32, BoardId::Wt32Eth01), Function::Rpwm, 13, IssueCode::Header);
    assertOnlyError(withPin(Function::Supply, 34, WT32, BoardId::Wt32Eth01), Function::Supply, 34, IssueCode::Header);
    // 12 is on the header (with its strap warning).
    TEST_ASSERT_TRUE(withPin(Function::ParkB, 12, WT32, BoardId::Wt32Eth01).valid());
}

void test_output_on_input_only_pin(void)
{
    for (Function f : {Function::Rpwm, Function::Lpwm, Function::Enable})
        for (uint8_t pin : {34, 35, 36, 39})
        {
            PinMap map = custom();
            // Move the analog inputs out of the way first, so only the output is wrong.
            map.currentR = 32;
            map.currentL = 33;
            map.supply = 36 == pin ? 35 : 36;
            map.hall = 39 == pin ? 34 : 39;
            if (map.supply == map.hall)
                map.hall = 34;
            map.parkA = 4;
            map.parkB = 13;
            map.setPin(f, pin);
            const PinReport report = check(map, DEVKIT);
            if (report.valid())
                TEST_FAIL_MESSAGE("an output on an input-only pin passed");
            TEST_ASSERT_EQUAL_STRING("inputOnly", issueCodeName(report.firstError()->code));
        }
}

void test_analog_off_adc1(void)
{
    for (Function f : {Function::CurrentR, Function::CurrentL, Function::Supply, Function::Hall})
        for (uint8_t pin : {4, 12, 13, 14})
            assertOnlyError(withPin(f, pin), f, pin, IssueCode::NotAdc1);
}

void test_park_switch_on_input_only_pin(void)
{
    PinMap map = custom();
    map.currentR = 32;
    map.parkA = 34;
    assertOnlyError(check(map, DEVKIT), Function::ParkA, 34, IssueCode::NoPullup);
}

void test_two_functions_on_one_pin(void)
{
    PinMap map = custom();
    map.lpwm = map.rpwm;
    const PinReport report = check(map, DEVKIT);
    assertOnlyError(report, Function::Lpwm, 25, IssueCode::Shared);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Function::Rpwm), static_cast<uint8_t>(report.firstError()->other));
    TEST_ASSERT_NOT_NULL(std::strstr(report.firstError()->text.c_str(), "already RPWM"));
}

void test_en_on_a_strap_or_boot_pulse_pin(void)
{
    for (uint8_t pin : {2, 5, 12, 14, 15}) // 0 is the probe's (test_probe_pins_on_every_board)
        assertOnlyError(withPin(Function::Enable, pin), Function::Enable, pin, IssueCode::EnStrap);
    // 4, 13 and the old 27 are quiet.
    TEST_ASSERT_TRUE(withPin(Function::Enable, 4).valid());
    TEST_ASSERT_TRUE(withPin(Function::Enable, 13).valid());
}

void test_named_preset_pins_are_fixed(void)
{
    PinMap map = presetMap(Preset::Devkit);
    map.rpwm = 13;
    assertOnlyError(check(map, DEVKIT), Function::Rpwm, 13, IssueCode::PresetMismatch);
    PinMap board = presetMap(Preset::Wt32CarrierV1);
    board.board = BoardId::Esp32Devkit;
    const PinReport report = check(board, DEVKIT);
    TEST_ASSERT_FALSE(report.valid());
    TEST_ASSERT_EQUAL_STRING("presetMismatch", issueCodeName(report.firstError()->code));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Function::None), static_cast<uint8_t>(report.firstError()->function));
}

void test_devkit_map_on_a_wt32_is_refused(void)
{
    PinMap map = presetMap(Preset::Devkit);
    map.preset = Preset::Custom;
    map.board = BoardId::Wt32Eth01;
    const PinReport report = check(map, WT32);
    TEST_ASSERT_FALSE(report.valid());
}

// --- Warnings: one per strap -------------------------------------------------

void test_strap_warnings(void)
{
    // GPIO0 is the probe's (an error); its strap text stays for completeness.
    TEST_ASSERT_TRUE(hasWarning(withPin(Function::ParkA, 2), Function::ParkA, IssueCode::Strap2));
    TEST_ASSERT_TRUE(hasWarning(withPin(Function::ParkA, 5), Function::ParkA, IssueCode::Strap5));
    TEST_ASSERT_TRUE(hasWarning(withPin(Function::ParkA, 12), Function::ParkA, IssueCode::Strap12));
    TEST_ASSERT_TRUE(hasWarning(withPin(Function::Rpwm, 15), Function::Rpwm, IssueCode::Strap15));
    for (uint8_t pin : {2, 5, 12})
        TEST_ASSERT_TRUE(withPin(Function::ParkA, pin).valid()); // warnings only
    // A warning is never an error.
    const PinReport report = withPin(Function::Lpwm, 15);
    TEST_ASSERT_TRUE(report.valid());
    TEST_ASSERT_FALSE(report.issues.front().error);
    TEST_ASSERT_NOT_NULL(std::strstr(report.issues.front().text.c_str(), "EN is held low"));
}

// --- Boot ------------------------------------------------------------------

void test_boot_valid_holds_nothing(void)
{
    const BootPins boot = bootPins(presetMap(Preset::Devkit), DEVKIT, false);
    TEST_ASSERT_TRUE(boot.valid);
    TEST_ASSERT_TRUE(boot.reason.empty());
    TEST_ASSERT_EQUAL_size_t(0, boot.holdAsInputs.size());
}

void test_boot_invalid_names_the_first_error_and_holds_candidates(void)
{
    PinMap map = custom();
    map.enable = 6; // a flash pin: never touched
    const BootPins boot = bootPins(map, DEVKIT, false);
    TEST_ASSERT_FALSE(boot.valid);
    TEST_ASSERT_EQUAL_STRING("EN on GPIO6: GPIO6 is a flash pin", boot.reason.c_str());
    // 25, 26 (saved), then the presets' 27, 14, 15, 4; never 6.
    const uint8_t expected[] = {25, 26, 27, 14, 15, 4};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), boot.holdAsInputs.size());
    for (size_t i = 0; i < sizeof(expected); ++i)
        TEST_ASSERT_EQUAL_UINT8(expected[i], boot.holdAsInputs[i]);
}

void test_boot_invalid_on_wt32_never_holds_ethernet_pins(void)
{
    PinMap map = custom(BoardId::Wt32Eth01);
    map.rpwm = 35; // input only
    const BootPins boot = bootPins(map, WT32, false);
    TEST_ASSERT_FALSE(boot.valid);
    for (uint8_t pin : boot.holdAsInputs)
    {
        TEST_ASSERT_FALSE(Facts::has(WT32.ethernet, pin));
        TEST_ASSERT_FALSE(Facts::has(WT32.inputOnly, pin) && pin != 35);
    }
    // 35 itself is held (an input already): harmless; 15 and 4 are held.
    bool held4 = false;
    for (uint8_t pin : boot.holdAsInputs)
        held4 = held4 || pin == 4;
    TEST_ASSERT_TRUE(held4);
}

void test_boot_unreadable_store_is_invalid_even_with_good_pins(void)
{
    const BootPins boot = bootPins(presetMap(Preset::Devkit), DEVKIT, true);
    TEST_ASSERT_FALSE(boot.valid);
    TEST_ASSERT_EQUAL_STRING("The saved pin setup couldn't be read", boot.reason.c_str());
    TEST_ASSERT_EQUAL_size_t(6, boot.holdAsInputs.size());
}

void test_unset_has_no_issues_but_boots_driving_nothing(void)
{
    const PinMap map = presetMap(Preset::Unset);
    TEST_ASSERT_EQUAL_STRING("unset", presetName(map.preset));
    TEST_ASSERT_EQUAL_size_t(0, check(map, DEVKIT).issues.size());
    const BootPins boot = bootPins(map, DEVKIT, false);
    TEST_ASSERT_FALSE(boot.valid);
    TEST_ASSERT_EQUAL_STRING("No board chosen yet: choose the board and its pins", boot.reason.c_str());
    // The DevKit's 25, 26, 27 and the carrier's 14, 15, 4 all left as inputs.
    TEST_ASSERT_EQUAL_size_t(6, boot.holdAsInputs.size());
    // A WT32 found by the probe: its Ethernet pins 25-27 are never touched.
    const BootPins wt32 = bootPins(map, WT32, false);
    TEST_ASSERT_FALSE(wt32.valid);
    for (uint8_t pin : wt32.holdAsInputs)
        TEST_ASSERT_FALSE(Facts::has(WT32.ethernet, pin));
    TEST_ASSERT_EQUAL_size_t(3, wt32.holdAsInputs.size());
}

void test_boot_never_holds_a_psram_pin(void)
{
    PinMap map = custom();
    map.rpwm = 16;
    const BootPins boot = bootPins(map, DEVKIT_PSRAM, false);
    TEST_ASSERT_FALSE(boot.valid);
    for (uint8_t pin : boot.holdAsInputs)
        TEST_ASSERT_NOT_EQUAL(16, pin);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_devkit_preset_is_the_old_map_and_the_default);
    RUN_TEST(test_wt32_carrier_preset_and_its_two_warnings);
    RUN_TEST(test_names_round_trip);
    RUN_TEST(test_missing_gpio);
    RUN_TEST(test_gpio_37_and_38_are_not_brought_out);
    RUN_TEST(test_probe_pins_on_every_board);
    RUN_TEST(test_flash_pins);
    RUN_TEST(test_psram_pins_on_every_board);
    RUN_TEST(test_a_detected_wt32_refuses_another_board);
    RUN_TEST(test_uart0);
    RUN_TEST(test_wt32_ethernet_pins);
    RUN_TEST(test_wt32_header);
    RUN_TEST(test_output_on_input_only_pin);
    RUN_TEST(test_analog_off_adc1);
    RUN_TEST(test_park_switch_on_input_only_pin);
    RUN_TEST(test_two_functions_on_one_pin);
    RUN_TEST(test_en_on_a_strap_or_boot_pulse_pin);
    RUN_TEST(test_named_preset_pins_are_fixed);
    RUN_TEST(test_devkit_map_on_a_wt32_is_refused);
    RUN_TEST(test_strap_warnings);
    RUN_TEST(test_boot_valid_holds_nothing);
    RUN_TEST(test_boot_invalid_names_the_first_error_and_holds_candidates);
    RUN_TEST(test_boot_invalid_on_wt32_never_holds_ethernet_pins);
    RUN_TEST(test_boot_unreadable_store_is_invalid_even_with_good_pins);
    RUN_TEST(test_unset_has_no_issues_but_boots_driving_nothing);
    RUN_TEST(test_boot_never_holds_a_psram_pin);
    return UNITY_END();
}
