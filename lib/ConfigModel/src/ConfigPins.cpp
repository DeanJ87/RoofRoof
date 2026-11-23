#include "ConfigPins.h"

#include "ConfigInternal.h"

namespace SQM
{
    namespace ConfigPins
    {
        using ConfigDetail::setError;

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
                    return setError(error, "Pins: preset must be devkit, wt32-carrier-v1 or custom");
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
