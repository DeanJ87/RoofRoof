// The demo's device core, exported to JavaScript: EmulatedDevice
// (emulated_device.h) holds what a RoofRoof would hold and answers the same
// requests with the same documents (SQMeter spec 016).

#include <emscripten/bind.h>

#include "emulated_device.h"

EMSCRIPTEN_BINDINGS(roofroof_core)
{
    emscripten::class_<EmulatedDevice>("EmulatedDevice")
        .constructor<std::string>()
        .function("getConfig", &EmulatedDevice::getConfig)
        .function("applyConfig", &EmulatedDevice::applyConfig)
        .function("loadConfig", &EmulatedDevice::loadConfig)
        .function("restart", &EmulatedDevice::restart)
        .function("tick", &EmulatedDevice::tick)
        .function("statusParts", &EmulatedDevice::statusParts)
        .function("inputsDocument", &EmulatedDevice::inputsDocument)
        .function("effective", &EmulatedDevice::effective)
        .function("pending", &EmulatedDevice::pending)
        .function("recentAlerts", &EmulatedDevice::recentAlerts)
        .function("clearAlerts", &EmulatedDevice::clearAlerts)
        .function("isArmed", &EmulatedDevice::isArmed)
        .function("armedDocument", &EmulatedDevice::armedDocument)
        .function("setArmed", &EmulatedDevice::setArmed)
        .function("testAlert", &EmulatedDevice::testAlert)
        .function("realTestAlert", &EmulatedDevice::realTestAlert)
        .function("deliveryRequests", &EmulatedDevice::deliveryRequests)
        .function("alpaca", &EmulatedDevice::alpaca)
        .function("backupRoute", &EmulatedDevice::backupRoute)
        .function("saveState", &EmulatedDevice::saveState)
        .function("loadState", &EmulatedDevice::loadState)
        .function("roofCommand", &EmulatedDevice::roofCommand)
        .function("calibration", &EmulatedDevice::calibration)
        .function("roofInject", &EmulatedDevice::roofInject)
        .function("roofPanel", &EmulatedDevice::roofPanel)
        .function("platformHold", &EmulatedDevice::platformHold)
        .function("diagDocument", &EmulatedDevice::diagDocument)
        .function("diagMoves", &EmulatedDevice::diagMoves)
        .function("diagMove", &EmulatedDevice::diagMove)
        .function("diagClear", &EmulatedDevice::diagClear);
}
