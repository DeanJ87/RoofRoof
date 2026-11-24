#include "Config.h"
#include "CopiedSecrets.h"
#include "ConfigInternal.h"

// Spec 024: the secrets a "copy from SQMeter" brings, swapped in at the save.
// The browser never sees them: a ticked row sends Config::COPIED_SECRET, and
// the device's held copy fills it, only for the server SQMeter used it for (a
// secret is only ever sent to the server it was copied for, spec 020 FR-017).

namespace SQM::ConfigDetail
{
    namespace
    {
        bool isCopied(const std::string &value)
        {
            return value == Config::COPIED_SECRET;
        }

        // Each field takes the held value only for the server it was read for.
        bool fillMqtt(MQTTConfig &m, const CopiedSecrets &held, std::string *error)
        {
            if (!isCopied(m.password))
                return true;
            if (m.broker != held.mqttBroker || m.port != held.mqttPort || m.username != held.mqttUsername)
                return setError(error, "The MQTT password from SQMeter is for SQMeter's own broker, not this one: copy again, or type it");
            if (held.mqttPassword.empty())
                return setError(error, "SQMeter has no MQTT password to copy: type it");
            m.password = held.mqttPassword;
            return true;
        }

        bool fillNtfy(AlertsConfig &a, const CopiedSecrets &held, std::string *error)
        {
            if (!isCopied(a.ntfyToken))
                return true;
            if (a.ntfyServer != held.ntfyServer)
                return setError(
                    error, "The ntfy token from SQMeter is for SQMeter's own ntfy server, not this one: copy again, or type it");
            if (held.ntfyToken.empty())
                return setError(error, "SQMeter has no ntfy token to copy: type it");
            a.ntfyToken = held.ntfyToken;
            return true;
        }

        // Pushover's server is always Pushover's.
        bool fillPushover(AlertsConfig &a, const CopiedSecrets &held, std::string *error)
        {
            const bool user = isCopied(a.pushoverUserKey);
            const bool app = isCopied(a.pushoverAppToken);
            if ((user && held.pushoverUserKey.empty()) || (app && held.pushoverAppToken.empty()))
                return setError(error, "SQMeter has no Pushover keys to copy: type them");
            if (user)
                a.pushoverUserKey = held.pushoverUserKey;
            if (app)
                a.pushoverAppToken = held.pushoverAppToken;
            return true;
        }

        // The secrets SQMeter never has: the placeholder there is refused, not stored.
        bool copiedElsewhere(const Config &cfg)
        {
            return isCopied(cfg.wifi.password) || isCopied(cfg.ota.password) || isCopied(cfg.auth.password) ||
                   isCopied(cfg.safety.httpPassword) || isCopied(cfg.alerts.webhookAuthHeader);
        }

        bool anyCopied(const Config &cfg)
        {
            const AlertsConfig &a = cfg.alerts;
            return isCopied(cfg.mqtt.password) || isCopied(a.ntfyToken) || isCopied(a.pushoverUserKey) || isCopied(a.pushoverAppToken);
        }
    } // namespace

    bool fillCopiedSecrets(Config &cfg, const CopiedSecrets *copied, std::string *error)
    {
        if (copiedElsewhere(cfg))
            return setError(error, "Only the MQTT password, the ntfy token and the Pushover keys can come from SQMeter");
        if (!anyCopied(cfg))
            return true;
        if (copied == nullptr)
            return setError(error, "A password from SQMeter is no longer held: copy again, or type it");
        return fillMqtt(cfg.mqtt, *copied, error) && fillNtfy(cfg.alerts, *copied, error) && fillPushover(cfg.alerts, *copied, error);
    }
} // namespace SQM::ConfigDetail
