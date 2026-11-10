#include <unity.h>

#include "FirmwareImage.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace SQM;
using FirmwareImage::Verdict;

namespace
{
    // An "image": filler, the marker somewhere in the middle, more filler.
    std::vector<uint8_t> image(const std::string &marker, size_t before = 5000, size_t after = 3000)
    {
        std::vector<uint8_t> bytes(before, 0x5A);
        bytes.insert(bytes.end(), marker.begin(), marker.end());
        bytes.insert(bytes.end(), after, 0xC3);
        return bytes;
    }

    FirmwareImage::Scanner scan(const std::vector<uint8_t> &bytes, size_t chunk)
    {
        FirmwareImage::Scanner scanner;
        for (size_t at = 0; at < bytes.size(); at += chunk)
            scanner.feed(bytes.data() + at, std::min(chunk, bytes.size() - at));
        return scanner;
    }

    std::string withFields(const std::string &fields)
    {
        const auto magic = FirmwareImage::magic();
        std::string out(magic.begin(), magic.end());
        out += fields;
        out.push_back('\0');
        return out;
    }

    // An SQMeter v0.4 image's marker: SQMeter's own magic and fields.
    std::string sqmeterMarker()
    {
        std::string out("\x43\x11\xB8\x37\xFA\xAE\x6D\x92\xD4\x0B\x81\x7C\xC9\x26\x9F\x55");
        out += "layout=l2;build=standard;";
        out.push_back('\0');
        return out;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_finds_marker_in_any_chunking()
{
    const auto bytes = image(FirmwareImage::marker());
    for (size_t chunk : {1u, 7u, 16u, 17u, 1024u, 100000u})
    {
        const auto scanner = scan(bytes, chunk);
        TEST_ASSERT_TRUE(scanner.found());
        TEST_ASSERT_EQUAL_STRING("roofroof", scanner.product().c_str());
        TEST_ASSERT_EQUAL_STRING("l2", scanner.layout().c_str());
        TEST_ASSERT_EQUAL_STRING("standard", scanner.build().c_str());
    }
}

void test_no_marker_in_an_image_without_one()
{
    std::vector<uint8_t> bytes(200000, 0xE9);
    TEST_ASSERT_FALSE(scan(bytes, 4096).found());
    TEST_ASSERT_EQUAL(Verdict::NoMarker, FirmwareImage::check(scan(bytes, 4096), "l2"));
}

// spec 001 US4-AC2: an SQMeter image is refused before anything is written.
void test_sqmeter_image_is_not_roofroof_firmware()
{
    const auto scanner = scan(image(sqmeterMarker()), 512);
    TEST_ASSERT_FALSE(scanner.found());
    TEST_ASSERT_EQUAL(Verdict::NoMarker, FirmwareImage::check(scanner, "l2"));
    TEST_ASSERT_EQUAL_STRING("Not RoofRoof firmware.", FirmwareImage::verdictMessage(Verdict::NoMarker));
}

void test_partial_magic_then_real_marker()
{
    // A run of the first magic bytes before the marker must not hide it.
    const auto magic = FirmwareImage::magic();
    std::string prefix(magic.begin(), magic.begin() + 9);
    const auto scanner = scan(image(prefix + prefix + FirmwareImage::marker()), 3);
    TEST_ASSERT_TRUE(scanner.found());
    TEST_ASSERT_EQUAL_STRING("roofroof", scanner.product().c_str());
}

void test_stray_magic_without_fields_is_skipped()
{
    const auto scanner = scan(image(withFields("") + std::string(40, 'x') + FirmwareImage::marker()), 64);
    TEST_ASSERT_TRUE(scanner.found());
    TEST_ASSERT_EQUAL_STRING("l2", scanner.layout().c_str());
}

void test_magic_bytes_never_repeat()
{
    // Scanner::feed restarts a match only at the current byte, which is only
    // right when no byte of the magic repeats.
    auto magic = FirmwareImage::magic();
    std::sort(magic.begin(), magic.end());
    TEST_ASSERT_TRUE(std::adjacent_find(magic.begin(), magic.end()) == magic.end());
}

void test_verdicts_check_product_then_layout_then_build()
{
    const auto ours = scan(image(FirmwareImage::marker()), 512);
    const auto otherProduct = scan(image(FirmwareImage::marker("sqmeter")), 512);
    const auto otherLayout = scan(image(FirmwareImage::marker(FirmwareImage::PRODUCT, "s16")), 512);
    const auto otherBuild = scan(image(withFields("product=roofroof;layout=l2;build=ble;")), 512);
    const auto everythingWrong = scan(image(withFields("product=sqmeter;layout=s16;build=ble;")), 512);

    TEST_ASSERT_EQUAL(Verdict::Ok, FirmwareImage::check(ours, "l2"));
    TEST_ASSERT_EQUAL(Verdict::OtherProduct, FirmwareImage::check(otherProduct, "l2"));
    TEST_ASSERT_EQUAL(Verdict::OtherLayout, FirmwareImage::check(otherLayout, "l2"));
    TEST_ASSERT_EQUAL(Verdict::OtherLayout, FirmwareImage::check(ours, "other"));
    TEST_ASSERT_EQUAL(Verdict::OtherBuild, FirmwareImage::check(otherBuild, "l2"));
    TEST_ASSERT_EQUAL(Verdict::OtherProduct, FirmwareImage::check(everythingWrong, "l2"));
}

void test_marker_fields_fit_the_scanner()
{
    const std::string marker = FirmwareImage::marker();
    const size_t fields = marker.size() - FirmwareImage::MAGIC_SIZE - 1; // without the NUL
    TEST_ASSERT_TRUE(fields <= FirmwareImage::MAX_FIELDS);
}

void test_messages_are_short()
{
    for (Verdict v : {Verdict::NoMarker, Verdict::OtherProduct, Verdict::OtherLayout, Verdict::OtherBuild})
    {
        const std::string message = FirmwareImage::verdictMessage(v);
        TEST_ASSERT_TRUE(!message.empty());
        TEST_ASSERT_TRUE(message.size() <= 90); // DS-22
    }
}

void test_layout_of_partition_table()
{
    TEST_ASSERT_EQUAL_STRING("l2", FirmwareImage::layoutOf(0x1C0000, 0x70000).c_str());
    TEST_ASSERT_EQUAL_STRING("other", FirmwareImage::layoutOf(0x180000, 0x80000).c_str());
    TEST_ASSERT_EQUAL_STRING("other", FirmwareImage::layoutOf(0x380000, 0xF0000).c_str());
}

void test_fs_image_must_fill_the_partition()
{
    TEST_ASSERT_TRUE(FirmwareImage::fsImageFits(0x70000 + 300, 0x70000, 4096));
    TEST_ASSERT_FALSE(FirmwareImage::fsImageFits(0x80000 + 300, 0x70000, 4096));
    TEST_ASSERT_FALSE(FirmwareImage::fsImageFits(0x60000, 0x70000, 4096));
}

void test_firmware_embeds_the_same_marker()
{
    // src/FirmwareMarker.cpp spells the magic out; it must match the library's.
    std::ifstream in("src/FirmwareMarker.cpp");
    TEST_ASSERT_TRUE_MESSAGE(in.good(), "run from the project root");
    std::stringstream source;
    source << in.rdbuf();
    std::string escaped;
    char buf[8];
    for (uint8_t b : FirmwareImage::magic())
    {
        snprintf(buf, sizeof(buf), "\\x%02X", b);
        escaped += buf;
    }
    TEST_ASSERT_TRUE_MESSAGE(source.str().find(escaped) != std::string::npos, escaped.c_str());
    TEST_ASSERT_TRUE(source.str().find("\"product=roofroof;layout=\" SQM_LAYOUT_ID \";build=standard;\"") != std::string::npos);
    // include/BuildTarget.h names the library's layout.
    std::ifstream target("include/BuildTarget.h");
    TEST_ASSERT_TRUE(target.good());
    std::stringstream targetSource;
    targetSource << target.rdbuf();
    TEST_ASSERT_TRUE(targetSource.str().find(std::string("#define SQM_LAYOUT_ID \"") + FirmwareImage::LAYOUT + "\"") != std::string::npos);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_finds_marker_in_any_chunking);
    RUN_TEST(test_no_marker_in_an_image_without_one);
    RUN_TEST(test_sqmeter_image_is_not_roofroof_firmware);
    RUN_TEST(test_partial_magic_then_real_marker);
    RUN_TEST(test_stray_magic_without_fields_is_skipped);
    RUN_TEST(test_magic_bytes_never_repeat);
    RUN_TEST(test_verdicts_check_product_then_layout_then_build);
    RUN_TEST(test_marker_fields_fit_the_scanner);
    RUN_TEST(test_messages_are_short);
    RUN_TEST(test_layout_of_partition_table);
    RUN_TEST(test_fs_image_must_fill_the_partition);
    RUN_TEST(test_firmware_embeds_the_same_marker);
    return UNITY_END();
}
