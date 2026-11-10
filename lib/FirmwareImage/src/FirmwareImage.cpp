#include "FirmwareImage.h"

namespace SQM
{
    namespace FirmwareImage
    {
        namespace
        {
            // The magic XOR SCRAMBLE. Unscrambled only into a local copy, so the
            // magic bytes appear exactly once in an image: in its marker.
            constexpr uint8_t SCRAMBLE = 0xA5;
            // RoofRoof's own magic, not SQMeter's: neither product's scanner
            // finds the other's marker. No byte repeats, so a mismatch can only
            // restart the match at the current byte (Scanner::feed).
            constexpr std::array<uint8_t, MAGIC_SIZE> SCRAMBLED = {
                0x1E, 0x75, 0xF0, 0x95, 0xD2, 0x78, 0xBA, 0x01, 0x42, 0xF8, 0x3B, 0x17, 0x87, 0xA9, 0xFB, 0x39};
        } // namespace

        std::string layoutOf(uint32_t appSlotSize, uint32_t fsSize)
        {
            return appSlotSize == APP_SLOT_SIZE && fsSize == FS_SIZE ? LAYOUT : OTHER_LAYOUT;
        }

        std::array<uint8_t, MAGIC_SIZE> magic()
        {
            std::array<uint8_t, MAGIC_SIZE> out{};
            for (size_t i = 0; i < MAGIC_SIZE; ++i)
                out[i] = static_cast<uint8_t>(SCRAMBLED[i] ^ SCRAMBLE);
            return out;
        }

        std::string marker(const char *product, const char *layout)
        {
            const auto bytes = magic();
            std::string out(bytes.begin(), bytes.end());
            out += std::string("product=") + product + ";layout=" + layout + ";build=" + BUILD + ";";
            out.push_back('\0');
            return out;
        }

        Scanner::Scanner()
            : pattern(magic())
        {
        }

        void Scanner::feed(const uint8_t *data, size_t len)
        {
            for (size_t i = 0; i < len && !complete; ++i)
            {
                const uint8_t byte = data[i];
                if (readingFields)
                {
                    if (byte == 0 || fields.size() >= MAX_FIELDS)
                    {
                        readingFields = false;
                        parseFields();
                    }
                    else
                    {
                        fields.push_back(static_cast<char>(byte));
                    }
                    continue;
                }
                // No byte of the magic repeats, so on a mismatch the only
                // possible restart is at this byte.
                if (byte == pattern[matched])
                    ++matched;
                else
                    matched = byte == pattern[0] ? 1 : 0;
                if (matched == MAGIC_SIZE)
                {
                    matched = 0;
                    readingFields = true;
                    fields.clear();
                }
            }
        }

        void Scanner::parseFields()
        {
            std::string product;
            std::string layout;
            std::string build;
            size_t start = 0;
            while (start < fields.size())
            {
                size_t end = fields.find(';', start);
                if (end == std::string::npos)
                    end = fields.size();
                const std::string pair = fields.substr(start, end - start);
                const size_t eq = pair.find('=');
                if (eq != std::string::npos)
                {
                    const std::string key = pair.substr(0, eq);
                    const std::string value = pair.substr(eq + 1);
                    if (key == "product")
                        product = value;
                    else if (key == "layout")
                        layout = value;
                    else if (key == "build")
                        build = value;
                }
                start = end + 1;
            }
            // A stray copy of the magic without fields isn't a marker; keep looking.
            if (product.empty() || layout.empty())
                return;
            productField = product;
            layoutField = layout;
            buildField = build;
            complete = true;
        }

        Verdict check(const Scanner &scanner, const std::string &deviceLayout)
        {
            if (!scanner.found())
                return Verdict::NoMarker;
            if (scanner.product() != PRODUCT)
                return Verdict::OtherProduct;
            if (scanner.layout() != deviceLayout)
                return Verdict::OtherLayout;
            if (scanner.build() != BUILD)
                return Verdict::OtherBuild;
            return Verdict::Ok;
        }

        const char *verdictMessage(Verdict verdict)
        {
            switch (verdict)
            {
            case Verdict::Ok:
                return "";
            case Verdict::NoMarker:
                return "Not RoofRoof firmware.";
            case Verdict::OtherProduct:
                return "This firmware is for another product.";
            case Verdict::OtherLayout:
                return "Built for a different partition layout: flash it over USB.";
            case Verdict::OtherBuild:
                return "Not a RoofRoof build.";
            }
            return "";
        }

        bool fsImageFits(size_t uploadBytes, size_t partitionSize, size_t overhead)
        {
            return uploadBytes >= partitionSize && uploadBytes <= partitionSize + overhead;
        }
    } // namespace FirmwareImage
} // namespace SQM
