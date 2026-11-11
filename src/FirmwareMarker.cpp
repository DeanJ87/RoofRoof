#include "FirmwareMarker.h"

#include <esp_partition.h>

#include "BuildTarget.h"

// The marker every image carries (lib/FirmwareImage). The magic is spelled out
// here and nowhere else, so an image contains it exactly once; the native test
// test_firmware_image checks it matches FirmwareImage::marker().
#define RR_MARKER_MAGIC "\xBB\xD0\x55\x30\x77\xDD\x1F\xA4\xE7\x5D\x9E\xB2\x22\x0C\x5E\x9C"
#define RR_MARKER_FIELDS "product=roofroof;layout=" SQM_LAYOUT_ID ";build=standard;"

extern "C" __attribute__((used)) const char RR_FIRMWARE_MARKER[] = RR_MARKER_MAGIC RR_MARKER_FIELDS;

namespace SQM
{
    namespace FirmwareMarker
    {
        const char *fields()
        {
            return RR_FIRMWARE_MARKER + FirmwareImage::MAGIC_SIZE;
        }

        const std::string &layout()
        {
            static const std::string value = []()
            {
                const esp_partition_t *app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
                const esp_partition_t *fs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
                return FirmwareImage::layoutOf(app ? app->size : 0, fs ? fs->size : 0);
            }();
            return value;
        }
    } // namespace FirmwareMarker
} // namespace SQM
