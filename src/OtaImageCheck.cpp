#include "OtaImageCheck.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "BenchMode.h"
#include "FirmwareImage.h"
#include "FirmwareMarker.h"

namespace SQM
{
    namespace OtaImageCheck
    {
        bool revertIfRefused(std::string &why)
        {
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *next = esp_ota_get_boot_partition();
            if (running == nullptr || next == nullptr || next == running)
                return false;
            // Read in small pieces from a static buffer: this runs on the main loop.
            static uint8_t buffer[512];
            FirmwareImage::Scanner scanner;
            for (size_t offset = 0; offset < next->size && !scanner.found(); offset += sizeof(buffer))
            {
                const size_t length = next->size - offset < sizeof(buffer) ? next->size - offset : sizeof(buffer);
                if (esp_partition_read(next, offset, buffer, length) != ESP_OK)
                    break;
                scanner.feed(buffer, length);
            }
            const FirmwareImage::Verdict verdict = FirmwareImage::check(scanner, FirmwareMarker::layout(), Bench::BUILD_NAME);
            if (verdict == FirmwareImage::Verdict::Ok)
                return false;
            why = FirmwareImage::verdictMessage(verdict, scanner.product());
            esp_ota_set_boot_partition(running);
            return true;
        }
    } // namespace OtaImageCheck
} // namespace SQM
