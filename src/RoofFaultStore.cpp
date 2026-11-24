#include "RoofFaultStore.h"

#include <nvs.h>

namespace SQM
{
    namespace RoofFaultStore
    {
        namespace
        {
            // The config's namespace (src/ConfigStore.cpp); its own key.
            const char *const NVS_NAMESPACE = "roofroof";
            const char *const KEY_FAULT = "roofFault";
            const char *const KEY_WATCH = "parkWatch"; // spec 024
        } // namespace

        RoofIo::Decoded load(RoofIo::FaultRecord &out)
        {
            nvs_handle_t handle;
            const esp_err_t opened = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
            if (opened == ESP_ERR_NVS_NOT_FOUND)
                return RoofIo::Decoded::None; // nothing ever saved in the namespace
            if (opened != ESP_OK)
            {
                // NVS itself failed: a fault may be stored and can't be read.
                out = RoofIo::storedUnreadableRecord();
                return RoofIo::Decoded::Unreadable;
            }
            uint8_t bytes[RoofIo::FAULT_RECORD_BYTES + 1] = {};
            size_t length = sizeof(bytes);
            const esp_err_t read = nvs_get_blob(handle, KEY_FAULT, bytes, &length);
            nvs_close(handle);
            if (read == ESP_ERR_NVS_NOT_FOUND)
                return RoofIo::Decoded::None;
            if (read != ESP_OK)
                length = sizeof(bytes); // too long or unreadable: damaged
            return RoofIo::decodeFault(bytes, length == 0 ? 1 : length, out);
        }

        bool save(const RoofIo::FaultRecord &record)
        {
            nvs_handle_t handle;
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
                return false;
            esp_err_t result;
            if (record.fault.kind == Roof::FaultKind::None)
            {
                result = nvs_erase_key(handle, KEY_FAULT);
                if (result == ESP_ERR_NVS_NOT_FOUND)
                    result = ESP_OK;
            }
            else
            {
                uint8_t bytes[RoofIo::FAULT_RECORD_BYTES];
                RoofIo::encodeFault(record, bytes);
                result = nvs_set_blob(handle, KEY_FAULT, bytes, sizeof(bytes));
            }
            if (result == ESP_OK)
                result = nvs_commit(handle);
            nvs_close(handle);
            return result == ESP_OK;
        }
        Safety::SwitchWatch loadWatch()
        {
            Safety::SwitchWatch watch;
            nvs_handle_t handle;
            if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
                return watch;
            size_t length = sizeof(watch.quiet);
            uint8_t bytes[sizeof(watch.quiet)] = {};
            if (nvs_get_blob(handle, KEY_WATCH, bytes, &length) == ESP_OK && length == sizeof(bytes))
            {
                watch.quiet[0] = bytes[0];
                watch.quiet[1] = bytes[1];
            }
            nvs_close(handle);
            return watch;
        }

        bool saveWatch(const Safety::SwitchWatch &watch)
        {
            nvs_handle_t handle;
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
                return false;
            esp_err_t result = nvs_set_blob(handle, KEY_WATCH, watch.quiet, sizeof(watch.quiet));
            if (result == ESP_OK)
                result = nvs_commit(handle);
            nvs_close(handle);
            return result == ESP_OK;
        }
    } // namespace RoofFaultStore
} // namespace SQM
