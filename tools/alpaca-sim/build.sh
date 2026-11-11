#!/usr/bin/env bash
# Builds ./alpaca-sim from the repo root. Needs ArduinoJson, which
# `pio test -e native` (or `pio pkg install -e native`) fetches.
# The Dome runs the real motion logic (lib/RoofLogic, lib/DriverLogic), the
# firmware's gates and Dome mapping (lib/RoofIoLogic) and the native tests'
# simulated roof (test/roof_sim).
set -euo pipefail
cd "$(dirname "$0")/../.."
ARDUINOJSON=.pio/libdeps/native/ArduinoJson/src
PIO=${PIO:-pio}
[ -d "$ARDUINOJSON" ] || "$PIO" pkg install -e native
c++ -std=gnu++17 -O1 -Wall -Wextra -Werror \
    -Ilib/AlpacaLogic/include -Ilib/RoofLogic/include -Ilib/RoofIoLogic/include -Ilib/DriverLogic/include \
    -Ilib/ConfigModel/include -Ilib/SafetyInputLogic/include -Ilib/NinaLogic/include -Itest/roof_sim -I"$ARDUINOJSON" \
    lib/AlpacaLogic/src/*.cpp lib/RoofLogic/src/*.cpp lib/DriverLogic/src/*.cpp \
    lib/RoofIoLogic/src/CommandCheck.cpp lib/RoofIoLogic/src/DomeBridge.cpp lib/RoofIoLogic/src/RoofGates.cpp lib/RoofIoLogic/src/RoofText.cpp \
    lib/RoofIoLogic/src/FaultRecord.cpp \
    tools/alpaca-sim/main.cpp \
    -o alpaca-sim
echo "Built ./alpaca-sim"
