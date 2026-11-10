#include "Config.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <nvs.h>
#include <cstring>

// Persisting the config in NVS. The model (defaults, JSON, validation) is in
// lib/ConfigModel so the native tests and the browser demo share it.

namespace SQM
{
    static const char *NVS_NAMESPACE = "roofroof";

    // Each document is a blob: an NVS string must fit in one 4 KB page, and on
    // a device whose pages are part used there's often no single page with
    // room for the config; a blob can span pages. The imaging-app alert
    // settings (Config::AlertsPart::Client) have their own key so both alerts
    // documents stay under the NVS limit.
    static const char *KEY_CONFIG = "configB";
    static const char *KEY_ALERTS = "alertsB";
    static const char *KEY_ALERTS_CLIENT = "alertclientB";

    namespace
    {
        // Reads a blob straight into heap memory. Preferences copies through a
        // variable-length array on the stack - ~2 KB for the config JSON, which
        // nearly overflowed the loop task during setup() on SQMeter.
        bool readNvsBlob(const char *key, std::string &out)
        {
            nvs_handle_t handle;
            if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
                return false;
            size_t length = 0;
            bool ok = nvs_get_blob(handle, key, nullptr, &length) == ESP_OK && length > 0;
            if (ok)
            {
                out.assign(length, '\0');
                ok = nvs_get_blob(handle, key, &out[0], &length) == ESP_OK;
                out.resize(length);
            }
            nvs_close(handle);
            return ok;
        }

        // Writes the document as a blob; an empty document removes it.
        bool writeDoc(Preferences &prefs, const char *key, const std::string &json)
        {
            if (json.empty())
            {
                prefs.remove(key);
                return true;
            }
            return prefs.putBytes(key, json.data(), json.size()) == json.size();
        }

        // A stored alerts part, or "" when there is none.
        std::string readStoredPart(const char *key)
        {
            std::string part;
            if (!readNvsBlob(key, part))
                part.clear();
            return part;
        }
    } // namespace

    bool Config::load(Config &out)
    {
        Logger::info(TAG, "Loading configuration from NVS");

        std::string json;
        if (!readNvsBlob(KEY_CONFIG, json) || json.empty())
        {
            Logger::warn(TAG, "No config found in NVS, creating default");
            out = createDefault();
            if (out.save())
            {
                Logger::info(TAG, "Default config saved successfully");
                return true;
            }
            Logger::error(TAG, "Failed to save default config");
            return false;
        }

        Logger::info(TAG, "Loaded config JSON (%u bytes)", static_cast<unsigned>(json.length()));

        // Alerts live under their own NVS keys; splice them into the main
        // document and parse once, straight into the caller's Config.
        const std::string alertsJson = readStoredPart(KEY_ALERTS);
        const std::string clientJson = readStoredPart(KEY_ALERTS_CLIENT);
        const bool haveParts = !alertsJson.empty() || !clientJson.empty();
        const std::string spliced = spliceStored(json, alertsJson, clientJson);

        out = createDefault();
        std::string reason;
        bool ok = applyJson(spliced, out, false, &reason);
        if (!ok)
            Logger::error(TAG, "Stored config rejected: %s", reason.c_str());
        if (!ok && haveParts)
        {
            // A corrupt alerts entry mustn't take the whole config down.
            Logger::error(TAG, "Failed to parse config JSON with alerts - retrying without them");
            out = createDefault();
            ok = applyJson(json, out, false);
        }

        if (ok)
            Logger::info(TAG, "Config parsed successfully - SSID: '%s'", out.wifi.ssid.c_str());
        else
            Logger::error(TAG, "Failed to parse config JSON");

        return ok;
    }

    bool Config::save() const
    {
        Logger::info(TAG, "Attempting to save configuration to NVS...");

        std::string validationError;
        if (!validate(&validationError))
        {
            Logger::error(TAG, "Refusing to save invalid configuration: %s", validationError.c_str());
            return false;
        }

        std::string json = toJson(false, false);
        const std::string alertsJson = alertsToJson(false, AlertsPart::Main);
        const std::string clientJson = alertsToJson(false, AlertsPart::Client);
        Logger::info(
            TAG,
            "Config JSON to save (%u bytes, alerts %u bytes)",
            static_cast<unsigned>(json.length()),
            static_cast<unsigned>(alertsJson.length()));

        if (json.length() > MAX_PERSISTED_JSON_BYTES)
        {
            Logger::error(
                TAG,
                "Config JSON too large for NVS (%u bytes, max %u bytes)",
                static_cast<unsigned>(json.length()),
                static_cast<unsigned>(MAX_PERSISTED_JSON_BYTES));
            return false;
        }

        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, false))
        {
            Logger::error(TAG, "Failed to open NVS namespace for writing");
            return false;
        }

        const bool alertsOk = writeDoc(prefs, KEY_ALERTS, alertsJson) && writeDoc(prefs, KEY_ALERTS_CLIENT, clientJson);
        const bool configOk = writeDoc(prefs, KEY_CONFIG, json);
        prefs.end();

        if (!alertsOk)
        {
            Logger::error(TAG, "Failed to write alerts config to NVS");
            return false;
        }

        if (!configOk)
        {
            Logger::error(TAG, "Failed to write config to NVS");
            return false;
        }

        Logger::info(TAG, "Configuration saved successfully to NVS (%u bytes)", static_cast<unsigned>(json.size()));

        return true;
    }

} // namespace SQM
