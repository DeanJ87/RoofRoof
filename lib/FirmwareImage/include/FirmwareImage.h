#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace SQM
{
    // Which product, partition layout and build a firmware image is for, and
    // whether this device may install it (SQMeter spec 027 FR-020, with a
    // product field added: specs/001-platform-bootstrap/contracts/firmware-marker.md).
    // Every image carries a marker: a fixed 16-byte magic followed by
    // "product=roofroof;layout=<id>;build=standard;" and a NUL. OTA scans the
    // image as it is written and refuses it before the boot partition is switched.
    namespace FirmwareImage
    {
        constexpr const char *PRODUCT = "roofroof";
        // The 4 MB layout of partitions.csv (SQMeter's "l2").
        constexpr const char *LAYOUT = "l2";
        // Its app slot and LittleFS sizes, to recognise it at run time.
        constexpr uint32_t APP_SLOT_SIZE = 0x1C0000;
        constexpr uint32_t FS_SIZE = 0x70000;
        // Any other partition table.
        constexpr const char *OTHER_LAYOUT = "other";
        constexpr const char *BUILD = "standard";

        // "l2" for the running partition table, otherwise "other".
        std::string layoutOf(uint32_t appSlotSize, uint32_t fsSize);

        constexpr size_t MAGIC_SIZE = 16;
        constexpr size_t MAX_FIELDS = 48;

        // The magic, stored scrambled so the scanner's own copy of it never
        // matches when an image is scanned.
        std::array<uint8_t, MAGIC_SIZE> magic();

        // The full marker for an image (magic + fields + NUL), as embedded by
        // the firmware (src/FirmwareMarker.cpp).
        std::string marker(const char *product = PRODUCT, const char *layout = LAYOUT);

        // Finds the marker in an image fed in chunks of any size.
        class Scanner
        {
        public:
            Scanner();
            void feed(const uint8_t *data, size_t len);
            bool found() const { return complete; }
            const std::string &product() const { return productField; }
            const std::string &layout() const { return layoutField; }
            const std::string &build() const { return buildField; }

        private:
            void parseFields();

            std::array<uint8_t, MAGIC_SIZE> pattern{};
            size_t matched = 0;
            bool readingFields = false;
            bool complete = false;
            std::string fields;
            std::string productField;
            std::string layoutField;
            std::string buildField;
        };

        enum class Verdict
        {
            Ok,
            NoMarker,     // not RoofRoof firmware (SQMeter's included: another magic)
            OtherProduct, // a marker naming another product
            OtherLayout,  // built for a layout this device doesn't have
            OtherBuild,   // a build this product doesn't make
        };

        // Checks product, then layout, then build; the first mismatch decides.
        Verdict check(const Scanner &scanner, const std::string &deviceLayout);

        // One line for the person updating (DS-22), English; the UI
        // translates it through the device-message templates.
        const char *verdictMessage(Verdict verdict);

        // A LittleFS image must fill this device's partition exactly.
        // `uploadBytes` is a multipart body's length: the image plus up to
        // `overhead` bytes of form framing.
        bool fsImageFits(size_t uploadBytes, size_t partitionSize, size_t overhead);
    } // namespace FirmwareImage
} // namespace SQM
