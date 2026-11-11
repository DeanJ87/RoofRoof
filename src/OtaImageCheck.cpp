#include "OtaImageCheck.h"

#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "BenchMode.h"
#include "FirmwareImage.h"
#include "FirmwareMarker.h"

namespace SQM
{
    namespace OtaImageCheck
    {
        namespace
        {
            // One flash sector: the image header that makes a slot bootable.
            constexpr size_t FLASH_SECTOR_BYTES = 4096;

            // The marker check over the new image only (its own length, so a
            // stale marker past its end can't count). NoMarker if the image
            // can't be read.
            FirmwareImage::Verdict checkImage(const esp_partition_t *next, std::string &product)
            {
                const esp_partition_pos_t position = {next->address, next->size};
                esp_image_metadata_t metadata = {};
                if (esp_image_get_metadata(&position, &metadata) != ESP_OK || metadata.image_len == 0 || metadata.image_len > next->size)
                    return FirmwareImage::Verdict::NoMarker;
                // Read in small pieces from a static buffer: this runs on the main loop.
                static uint8_t buffer[512];
                FirmwareImage::Scanner scanner;
                for (size_t offset = 0; offset < metadata.image_len && !scanner.found(); offset += sizeof(buffer))
                {
                    const size_t left = metadata.image_len - offset;
                    const size_t length = left < sizeof(buffer) ? left : sizeof(buffer);
                    if (esp_partition_read(next, offset, buffer, length) != ESP_OK)
                        return FirmwareImage::Verdict::NoMarker;
                    scanner.feed(buffer, length);
                }
                product = scanner.product();
                return FirmwareImage::check(scanner, FirmwareMarker::layout(), Bench::BUILD_NAME);
            }
        } // namespace

        // Update.end() has made the new image the boot partition. The running
        // firmware is put back first, so a power cut during the check boots
        // it; the new image is booted only once it passes. A refused image's
        // first sector is erased, so the bootloader can never fall back to it
        // (a rollback from a failed boot of the running firmware included).
        bool revertIfRefused(std::string &why)
        {
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *next = esp_ota_get_boot_partition();
            if (running == nullptr || next == nullptr || next == running)
                return false;
            const esp_err_t back = esp_ota_set_boot_partition(running);
            std::string product;
            const FirmwareImage::Verdict verdict = checkImage(next, product);
            if (verdict == FirmwareImage::Verdict::Ok)
            {
                if (esp_ota_set_boot_partition(next) == ESP_OK)
                    return false;
                why = "the new firmware couldn't be made the boot partition";
                return true;
            }
            why = FirmwareImage::verdictMessage(verdict, product);
            const esp_err_t erased = esp_partition_erase_range(next, 0, FLASH_SECTOR_BYTES);
            if (back != ESP_OK || erased != ESP_OK)
                why += " (WARNING: couldn't fully undo it: flash the standard firmware over USB)";
            return true;
        }
    } // namespace OtaImageCheck
} // namespace SQM
