#include "BackupInternal.h"
#include "ConfigBackup.h"

#include <ArduinoJson.h>

#include <utility>

// The restore preview: the candidate config a restore would save, built and
// validated exactly as a settings save (SQMeter spec 033 FR-007,
// FR-009, FR-011, FR-012). The device decides what is applied; the web UI
// lists the differences from the candidate's masked settings.

namespace SQM::Backup
{
    using namespace Detail;

    namespace
    {
        // Pushover on in the backup, but no keys to send with: the save would be refused.
        void markPushover(JsonObject settings, const PreviewInput &in, const Config &current, std::vector<NotApplicable> &out)
        {
            if (in.secrets || (!current.alerts.pushoverUserKey.empty() && !current.alerts.pushoverAppToken.empty()))
                return;
            JsonObject pushover = settings["alerts"]["pushover"].as<JsonObject>();
            if (!pushover["enabled"].is<bool>() || !pushover["enabled"].as<bool>())
                return;
            out.push_back({"alerts.pushover.enabled", "true", NotApplicableReason::PushoverKeys});
            pushover.remove("enabled");
        }

        // Where a secret goes in the settings document, made if missing.
        JsonVariant secretSlot(JsonObject settings, const std::string &path)
        {
            JsonObject node = settings;
            size_t start = 0;
            for (size_t dot = path.find('.'); dot != std::string::npos; start = dot + 1, dot = path.find('.', start))
            {
                const std::string key = path.substr(start, dot - start);
                JsonObject child = node[key].as<JsonObject>();
                node = child.isNull() ? node.createNestedObject(key) : child;
            }
            const std::string key = path.substr(start);
            if (!node.containsKey(key))
                node[key] = nullptr; // a member must exist before a variant can point at it
            return node[key];
        }

        // The decrypted secrets replace the masks; an empty one clears the setting.
        void injectSecrets(JsonObject settings, const Secrets &secrets)
        {
            for (const auto &secret : secrets)
            {
                JsonVariant target = secretSlot(settings, secret.first);
                if (secret.second.empty())
                    target.set(nullptr);
                else
                    target.set(secret.second);
            }
        }

        void secretStatus(const PreviewInput &in, const Config &current, Preview &out)
        {
            if (!in.secrets)
                return;
            for (size_t i = 0; i < SECRET_COUNT; ++i)
            {
                const bool same = secretRef(current, i) == secretRef(out.candidate, i);
                (same ? out.secretsUnchanged : out.secretsChanged) |= static_cast<uint16_t>(1U << i);
            }
        }

        // The backup's settings as they will be applied: what can't apply removed, secrets in.
        Error prepareSettings(const PreviewInput &in, const Config &current, Preview &out, std::string &applyText)
        {
            DynamicJsonDocument doc(jsonCapacity(in.parsed->settings, 2048)); // + the secrets put in
            if (!parseCopy(in.parsed->settings, doc) || !doc.is<JsonObject>())
                return Error::Damaged;
            JsonObject settings = doc.as<JsonObject>();
            markPushover(settings, in, current, out.notApplicable);
            if (in.secrets)
                injectSecrets(settings, *in.secrets);
            serializeJson(settings, applyText);
            return Error::None;
        }

        const char *secretsStateName(SecretsState state)
        {
            switch (state)
            {
            case SecretsState::Restored:
                return "restored";
            case SecretsState::Kept:
                return "kept";
            case SecretsState::None:
                break;
            }
            return "none";
        }

        void writeHeader(JsonObject target, const Parsed &parsed)
        {
            target["format"] = parsed.format;
            const std::pair<const char *, const std::string *> fields[] = {
                {"firmware", &parsed.firmware},
                {"board", &parsed.board},
                {"build", &parsed.build},
                {"deviceName", &parsed.deviceName},
                {"createdAt", &parsed.createdAt},
            };
            for (const auto &field : fields)
                if (!field.second->empty())
                    target[field.first] = field.second->c_str();
        }

        void writeSecretList(JsonArray target, uint16_t bits)
        {
            for (size_t i = 0; i < SECRET_COUNT; ++i)
                if ((bits >> i) & 1U)
                    target.add(SECRET_PATHS[i]);
        }
    } // namespace

    Error buildPreview(const PreviewInput &in, Preview &out, std::string *error)
    {
        const Config &current = *in.current;
        out.secretsState = in.secrets ? SecretsState::Restored : (in.parsed->hasSecrets ? SecretsState::Kept : SecretsState::None);
        std::string applyText;
        const Error prepared = prepareSettings(in, current, out, applyText);
        if (prepared != Error::None)
            return prepared;
        // The same rules as POST /api/config: masks keep secrets, types are strict, validate() runs.
        if (!Config::applyJson(applyText, out.candidate, true, error))
            return Error::Invalid;
        secretStatus(in, current, out);
        out.restart = restartReasons(current, out.candidate);
        return Error::None;
    }

    std::string previewJson(const Preview &preview, const Parsed &parsed, const std::string &token)
    {
        const std::string settings = preview.candidate.toJson(true);
        DynamicJsonDocument doc(1536 + 96 * preview.notApplicable.size());
        doc["token"] = token.c_str();
        writeHeader(doc.createNestedObject("backup"), parsed);
        doc["settings"] = serialized(settings.c_str(), settings.size()); // referenced: `settings` outlives the document
        JsonObject secrets = doc.createNestedObject("secrets");
        secrets["state"] = secretsStateName(preview.secretsState);
        writeSecretList(secrets.createNestedArray("changed"), preview.secretsChanged);
        writeSecretList(secrets.createNestedArray("unchanged"), preview.secretsUnchanged);
        JsonArray notApplicable = doc.createNestedArray("notApplicable");
        for (const NotApplicable &n : preview.notApplicable)
        {
            JsonObject item = notApplicable.createNestedObject();
            item["path"] = n.path.c_str();
            if (!n.value.empty())
                item["value"] = serialized(n.value.c_str(), n.value.size());
            item["reason"] = reasonCode(n.reason);
        }
        JsonArray restart = doc.createNestedArray("restart");
        for (int i = 0; i < RESTART_REASON_COUNT; ++i)
            if (preview.restart & (1U << i))
                restart.add(restartCode(i));
        std::string out;
        serializeJson(doc, out);
        return out;
    }
} // namespace SQM::Backup
