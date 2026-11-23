#include "ConfigPins.h"

#include "ConfigInternal.h"

#include <cstring>
#include <utility>

namespace SQM
{
    namespace ConfigPins
    {
        using ConfigDetail::setError;

        namespace
        {
            void writeIssues(JsonArray out, const Board::PinReport &report)
            {
                for (const Board::Issue &issue : report.issues)
                {
                    JsonObject item = out.createNestedObject();
                    if (issue.function == Board::Function::None)
                        item["function"] = nullptr;
                    else
                        item["function"] = Board::functionName(issue.function);
                    item["pin"] = issue.pin;
                    item["code"] = Board::issueCodeName(issue.code);
                    item["error"] = issue.error;
                    item["text"] = issue.text;
                }
            }

            void writeBoards(JsonArray out)
            {
                for (Board::BoardId board : {Board::BoardId::Esp32Devkit, Board::BoardId::Wt32Eth01})
                {
                    const Board::Facts facts = Board::facts(board, false);
                    JsonObject item = out.createNestedObject();
                    item["board"] = Board::boardName(board);
                    JsonArray ethernet = item.createNestedArray("ethernet");
                    JsonArray header = item.createNestedArray("header");
                    for (unsigned pin = 0; pin < 40; ++pin)
                    {
                        if (Board::Facts::has(facts.ethernet, pin))
                            ethernet.add(pin);
                        if (Board::Facts::has(facts.header, pin))
                            header.add(pin);
                    }
                }
            }
            void writeAnalogIssues(JsonArray out, const Board::AnalogConfig &analog, bool &valid)
            {
                for (const Board::AnalogIssue &issue : Board::checkAnalog(analog))
                {
                    JsonObject item = out.createNestedObject();
                    item["input"] = issue.input;
                    item["error"] = issue.error;
                    item["text"] = issue.text;
                    valid = valid && !issue.error;
                }
            }
        } // namespace

        void writePinsDocument(JsonObject out, const PinsView &view)
        {
            write(out.createNestedObject("running"), view.running);
            write(out.createNestedObject("saved"), view.saved);
            out["restartNeeded"] = view.saved != view.running;
            out["valid"] = view.boot.valid;
            out["reason"] = view.boot.reason;
            writeIssues(out.createNestedArray("issues"), view.boot.report);
            out["psram"] = view.psram;
            // Spec 023 Advanced: the dividers and trims, their warnings, and the presets.
            writeAnalog(out.createNestedObject("analog"), view.analog);
            bool analogValid = true;
            writeAnalogIssues(out.createNestedArray("analogIssues"), view.analog, analogValid);
            JsonArray analogPresets = out.createNestedArray("analogPresets");
            for (Board::AnalogPreset preset : {Board::AnalogPreset::DevkitBench, Board::AnalogPreset::Wt32CarrierV11})
                writeAnalog(analogPresets.createNestedObject(), Board::analogPreset(preset));
            out["board"] = Board::boardName(view.board);
            // FR-034: with no board chosen, what the boot probe found (null: it couldn't run).
            if (view.probed)
                out["detected"] = Board::boardName(view.detected);
            else
                out["detected"] = nullptr;
            JsonArray presets = out.createNestedArray("presets");
            for (Board::Preset preset : {Board::Preset::Devkit, Board::Preset::Wt32CarrierV1})
                write(presets.createNestedObject(), Board::presetMap(preset));
            writeBoards(out.createNestedArray("boards"));
        }

        void writeValidation(JsonObject out, const Board::PinMap &map, const Board::PinReport &report, const Board::AnalogConfig *analog)
        {
            bool valid = report.valid();
            write(out.createNestedObject("pins"), map);
            writeIssues(out.createNestedArray("issues"), report);
            if (analog != nullptr)
            {
                writeAnalog(out.createNestedObject("analog"), *analog);
                writeAnalogIssues(out.createNestedArray("analogIssues"), *analog, valid);
            }
            out["valid"] = valid;
        }

        void writeWiring(JsonObject out, CheckState state, const Board::WiringResult &result, uint32_t finishedAtMs)
        {
            out["state"] = state == CheckState::Running ? "running" : state == CheckState::Done ? "done" : "idle";
            JsonArray checks = out.createNestedArray("checks");
            if (state != CheckState::Done)
                return;
            for (const Board::Check &check : result.checks)
            {
                JsonObject item = checks.createNestedObject();
                item["id"] = Board::checkIdName(check.id);
                item["level"] = Board::levelName(check.level);
                if (std::isfinite(check.value))
                    item["value"] = check.value;
                else
                    item["value"] = nullptr;
                item["unit"] = check.unit;
                item["text"] = check.text;
            }
            if (result.suggestWt32)
                out["suggestPreset"] = Board::presetName(Board::Preset::Wt32CarrierV1);
            out["finishedAt"] = finishedAtMs;
        }

        namespace
        {
            void writeDivider(JsonObject out, const Board::Divider &d)
            {
                out["top"] = d.topOhms;
                out["bottom"] = d.bottomOhms;
            }

            void writeTrim(JsonObject out, const Board::Trim &t, const char *offsetKey)
            {
                out["gain"] = t.gain;
                out[offsetKey] = t.offset;
            }

            bool readDivider(JsonObjectConst in, const char *key, Board::Divider &d, std::string *error)
            {
                if (!in.containsKey(key))
                    return true;
                JsonObjectConst o = in[key];
                for (const auto &field : {std::make_pair("top", &d.topOhms), std::make_pair("bottom", &d.bottomOhms)})
                {
                    if (!o.containsKey(field.first))
                        continue;
                    if (!o[field.first].is<uint32_t>())
                        return setError(error, std::string("Analog: ") + key + "." + field.first + " must be a whole number of ohms");
                    *field.second = o[field.first].as<uint32_t>();
                }
                return true;
            }

            bool readTrim(JsonObjectConst in, const char *key, const char *offsetKey, Board::Trim &t, std::string *error)
            {
                if (!in.containsKey(key))
                    return true;
                JsonObjectConst o = in[key];
                for (const auto &field : {std::make_pair("gain", &t.gain), std::make_pair(offsetKey, &t.offset)})
                {
                    if (!o.containsKey(field.first))
                        continue;
                    if (!o[field.first].is<float>())
                        return setError(error, std::string("Analog: ") + key + "." + field.first + " must be a number");
                    *field.second = o[field.first].as<float>();
                }
                return true;
            }
        } // namespace

        void writeAnalog(JsonObject out, const Board::AnalogConfig &a)
        {
            out["preset"] = Board::analogPresetName(a.preset);
            JsonObject supply = out.createNestedObject("supply");
            writeDivider(supply, a.supply);
            writeTrim(supply, a.supplyTrim, "offsetVolts");
            writeDivider(out.createNestedObject("hall"), a.hall);
            JsonObject r = out.createNestedObject("currentR");
            writeDivider(r, a.currentR);
            writeTrim(r, a.currentRTrim, "offsetAmps");
            JsonObject l = out.createNestedObject("currentL");
            writeDivider(l, a.currentL);
            writeTrim(l, a.currentLTrim, "offsetAmps");
        }

        bool readAnalog(JsonObjectConst in, Board::AnalogConfig &a, std::string *error)
        {
            if (in.containsKey("preset"))
            {
                Board::AnalogPreset preset = Board::AnalogPreset::Custom;
                const char *name = in["preset"].is<const char *>() ? in["preset"].as<const char *>() : "";
                if (!Board::parseAnalogPreset(name, preset))
                    return setError(error, "Analog: preset must be devkit-bench, wt32-carrier-v1.1 or custom");
                // A named preset sets every value first; values sent with it are applied on top.
                if (Board::isNamed(preset))
                    a = Board::analogPreset(preset);
                a.preset = preset;
            }
            return readDivider(in, "supply", a.supply, error) && readTrim(in, "supply", "offsetVolts", a.supplyTrim, error) &&
                   readDivider(in, "hall", a.hall, error) && readDivider(in, "currentR", a.currentR, error) &&
                   readTrim(in, "currentR", "offsetAmps", a.currentRTrim, error) && readDivider(in, "currentL", a.currentL, error) &&
                   readTrim(in, "currentL", "offsetAmps", a.currentLTrim, error);
        }

        void write(JsonObject out, const Board::PinMap &map)
        {
            out["preset"] = Board::presetName(map.preset);
            out["board"] = Board::boardName(map.board);
            for (size_t i = 0; i < Board::FUNCTION_COUNT; ++i)
            {
                const auto function = static_cast<Board::Function>(i);
                out[Board::functionName(function)] = map.pin(function);
            }
            out["parkAPullup"] = map.parkAExternalPullup ? "external" : "internal";
            out["parkBPullup"] = map.parkBExternalPullup ? "external" : "internal";
        }

        bool read(JsonObjectConst in, Board::PinMap &map, std::string *error)
        {
            if (in.containsKey("preset"))
            {
                Board::Preset preset = Board::Preset::Custom;
                const char *name = in["preset"].is<const char *>() ? in["preset"].as<const char *>() : "";
                if (!Board::parsePreset(name, preset))
                    return setError(error, "Pins: preset must be devkit, wt32-carrier-v1, custom or unset");
                if (Board::isNamed(preset))
                {
                    // The pull-ups aren't part of a preset: they stay.
                    const bool pullA = map.parkAExternalPullup;
                    const bool pullB = map.parkBExternalPullup;
                    map = Board::presetMap(preset);
                    map.parkAExternalPullup = pullA;
                    map.parkBExternalPullup = pullB;
                }
                map.preset = preset;
            }
            if (in.containsKey("board"))
            {
                const char *name = in["board"].is<const char *>() ? in["board"].as<const char *>() : "";
                if (!Board::parseBoard(name, map.board))
                    return setError(error, "Pins: board must be esp32-devkit or wt32-eth01");
            }
            for (size_t i = 0; i < Board::FUNCTION_COUNT; ++i)
            {
                const auto function = static_cast<Board::Function>(i);
                const char *key = Board::functionName(function);
                if (!in.containsKey(key))
                    continue;
                JsonVariantConst value = in[key];
                if (!value.is<uint8_t>())
                    return setError(error, std::string("Pins: ") + key + " must be a GPIO number");
                map.setPin(function, value.as<uint8_t>());
            }
            for (const auto &pull :
                 {std::make_pair("parkAPullup", &map.parkAExternalPullup), std::make_pair("parkBPullup", &map.parkBExternalPullup)})
            {
                if (!in.containsKey(pull.first))
                    continue;
                const char *value = in[pull.first].is<const char *>() ? in[pull.first].as<const char *>() : "";
                if (std::strcmp(value, "internal") != 0 && std::strcmp(value, "external") != 0)
                    return setError(error, std::string("Pins: ") + pull.first + " must be internal or external");
                *pull.second = std::strcmp(value, "external") == 0;
            }
            return true;
        }
    } // namespace ConfigPins
} // namespace SQM
