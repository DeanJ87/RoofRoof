// The Ethernet probe's MDIO frames and decoding (spec 023 FR-018, research R7).

#include <unity.h>

#include "Mdio.h"

using namespace SQM::Board;

void setUp(void) {}
void tearDown(void) {}

void test_read_frame_bits(void)
{
    bool bits[Mdio::DRIVEN_BITS];
    Mdio::readFrame(1, Mdio::REG_ID1, bits);
    TEST_ASSERT_EQUAL_size_t(46, Mdio::DRIVEN_BITS);
    for (size_t i = 0; i < 32; ++i)
        TEST_ASSERT_TRUE(bits[i]); // preamble
    // Start 01, read 10.
    TEST_ASSERT_FALSE(bits[32]);
    TEST_ASSERT_TRUE(bits[33]);
    TEST_ASSERT_TRUE(bits[34]);
    TEST_ASSERT_FALSE(bits[35]);
    // PHY address 1 = 00001, register 2 = 00010.
    const bool rest[] = {false, false, false, false, true, false, false, false, true, false};
    for (size_t i = 0; i < 10; ++i)
        TEST_ASSERT_EQUAL(rest[i], bits[36 + i]);
}

void test_read_frame_masks_to_five_bits(void)
{
    bool bits[Mdio::DRIVEN_BITS];
    Mdio::readFrame(31, 31, bits);
    for (size_t i = 36; i < 46; ++i)
        TEST_ASSERT_TRUE(bits[i]);
}

void test_decode_read(void)
{
    // Turnaround Z0, then 0xC0F1.
    bool reply[Mdio::REPLY_BITS] = {true, false};
    const uint16_t value = 0xC0F1;
    for (size_t i = 0; i < 16; ++i)
        reply[2 + i] = ((value >> (15 - i)) & 1u) != 0;
    uint16_t out = 0;
    TEST_ASSERT_TRUE(Mdio::decodeRead(reply, out));
    TEST_ASSERT_EQUAL_HEX16(0xC0F1, out);
    // Nobody drove the turnaround: the pull-up reads high.
    reply[1] = true;
    TEST_ASSERT_FALSE(Mdio::decodeRead(reply, out));
}

void test_identify(void)
{
    TEST_ASSERT_EQUAL(Mdio::Phy::Lan87xx, Mdio::identify(0x0007, 0xC0F0));
    TEST_ASSERT_EQUAL(Mdio::Phy::Lan87xx, Mdio::identify(0x0007, 0xC0FF));
    TEST_ASSERT_EQUAL(Mdio::Phy::Other, Mdio::identify(0x0007, 0xC110));
    TEST_ASSERT_EQUAL(Mdio::Phy::Other, Mdio::identify(0x0022, 0x1555));
    TEST_ASSERT_EQUAL(Mdio::Phy::None, Mdio::identify(0xFFFF, 0xFFFF));
    TEST_ASSERT_EQUAL(Mdio::Phy::None, Mdio::identify(0x0000, 0x0000));
}

void test_clock_seen(void)
{
    const bool still[] = {true, true, true, true};
    const bool running[] = {true, false, true, true};
    TEST_ASSERT_FALSE(Mdio::clockSeen(still, 4));
    TEST_ASSERT_TRUE(Mdio::clockSeen(running, 4));
    TEST_ASSERT_FALSE(Mdio::clockSeen(nullptr, 4));
    TEST_ASSERT_FALSE(Mdio::clockSeen(running, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_frame_bits);
    RUN_TEST(test_read_frame_masks_to_five_bits);
    RUN_TEST(test_decode_read);
    RUN_TEST(test_identify);
    RUN_TEST(test_clock_seen);
    return UNITY_END();
}
