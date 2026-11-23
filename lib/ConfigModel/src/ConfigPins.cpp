#include "ConfigPins.h"

#include "ConfigInternal.h"

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

        void writeValidation(JsonObject out, const Board::PinMap &map, const Board::PinReport &report)
        {
            out["valid"] = report.valid();
            write(out.createNestedObject("pins"), map);
            writeIssues(out.createNestedArray("issues"), report);
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

        void write(JsonObject out, const Board::PinMap &map)
        {
            out["preset"] = Board::presetName(map.preset);
            out["board"] = Board::boardName(map.board);
            for (size_t i = 0; i < Board::FUNCTION_COUNT; ++i)
            {
                const auto function = static_cast<Board::Function>(i);
                out[Board::functionName(function)] = map.pin(function);
            }
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
                    map = Board::presetMap(preset);
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
            return true;
        }
    } // namespace ConfigPins
} // namespace SQM
