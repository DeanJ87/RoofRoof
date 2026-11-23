#include "DualStackClient.h"
#include "Logger.h"
#include "NetAddress.h"
#include "PlatformGuard.h"
#include "WebServer.h"
#include "WebServerShared.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstring>

// The REST API: settings, status, restart, WiFi and the MQTT test.

namespace SQM
{
    using namespace WebShared;

    void WebServer::setupAPIRoutes()
    {
        setupBackupRoutes();
        setupRoofRoutes();
        setupDiagRoutes();
        setupSiteRoutes();
        server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) { handleGetStatus(request); });
        // What's actually in effect (SQMeter spec 020).
        server.on("/api/settings/effective", HTTP_GET, [this](AsyncWebServerRequest *request) { handleSettingsEffective(request); });
        setupSettingsRoutes();
        setupNetworkRoutes();
    }

    void WebServer::setupSettingsRoutes()
    {
        server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *request) { handleGetConfig(request); });
        AsyncCallbackJsonWebHandler *configHandler = new AsyncCallbackJsonWebHandler(
            "/api/config",
            [this](AsyncWebServerRequest *request, JsonVariant &json) { handleSetConfig(request, json); },
            CONFIG_JSON_BUFFER_SIZE);
        configHandler->setMethod(HTTP_POST | HTTP_PUT);
        server.addHandler(configHandler);

        server.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request) { handleRestart(request); });
    }

    void WebServer::setupNetworkRoutes()
    {
        server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) { handleWiFiScan(request); });

        server.addHandler(new AsyncCallbackJsonWebHandler(
            "/api/mqtt/test", [this](AsyncWebServerRequest *request, JsonVariant &json) { handleMQTTTest(request, json); }));
        server.addHandler(new AsyncCallbackJsonWebHandler(
            "/api/wifi/connect", [this](AsyncWebServerRequest *request, JsonVariant &json) { handleWiFiConnect(request, json); }));
    }

    void WebServer::handleSettingsEffective(AsyncWebServerRequest *request)
    {
        const Deps::Facts facts = settingsFacts();
        const std::vector<Deps::Entry> entries = Deps::evaluate(getConfigCallback(), facts);
        DynamicJsonDocument doc(Deps::reportCapacity(entries));
        Deps::writeReport(doc.to<JsonObject>(), entries, facts);
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    }

    namespace
    {
        // Spec 022: the test box's deadline is the device's (ConfigSave::stampTestBox).
        bool stampTestBox(const Config &live, Config &merged, const std::string &body, std::string &reason)
        {
            const int64_t epoch = static_cast<int64_t>(time(nullptr));
            TestBoxConfig &box = merged.testBox;
            if (!ConfigSave::stampTestBox(
                    live.testBox,
                    box,
                    ConfigSave::sendsTestBoxHours(body),
                    ConfigSave::Clock{epoch >= Core::CLOCK_VALID_EPOCH, static_cast<uint32_t>(epoch)},
                    &reason))
                return false;
            if (box.any() && box.until != live.testBox.until)
                Logger::warn(
                    "WebServer",
                    "Test box bypasses saved: park %d, weather %d, daylight %d, until %s",
                    box.park,
                    box.weather,
                    box.daylight,
                    TestBox::formatUtc(box.until).c_str());
            return true;
        }
    } // namespace

    void WebServer::handleSetConfig(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request))
            return;

        std::string body;
        serializeJson(json, body);
        // Spec 021: checked and merged onto the live settings under the save
        // lock, so no other save lands in between; a page's stale fields refused.
        std::string reason;
        ConfigSave::Check stale;
        const bool saved = editAsFlashWrite(
            request,
            [&](const Config &live, Config &next)
            {
                stale = ConfigSave::checkSave(body, configRevision.load(), [&live]() { return live.toJson(true); });
                if (stale.stale)
                    return false;
                auto merged = Config::fromJson(body, &live, &reason);
                if (!merged)
                {
                    reason = reason.empty() ? "Invalid configuration" : reason;
                    return false;
                }
                if (!stampTestBox(live, *merged, body, reason))
                    return false;
                next = std::move(*merged);
                return true;
            });
        if (saved || !reason.empty() || stale.stale)
            answerConfigSave(request, saved, reason, stale);
    }

    void WebServer::answerConfigSave(AsyncWebServerRequest *request, bool saved, const std::string &reason, const ConfigSave::Check &stale)
    {
        // An action's answer (contracts/action-result): `success` only when saved.
        DynamicJsonDocument doc(512 + 64 * stale.conflicts.size());
        if (saved)
            doc["success"] = true;
        doc["revision"] = configRevision.load();
        int code = 200;
        if (stale.stale)
        {
            code = 409;
            const std::string text = ConfigSave::conflictText(stale);
            Logger::warn(TAG, "Config save refused: %s", text.c_str());
            doc["error"] = text;
            JsonArray names = doc.createNestedArray("conflicts");
            for (const std::string &name : stale.conflicts)
                names.add(name);
        }
        else if (!saved)
        {
            code = 400;
            Logger::warn(TAG, "Config rejected: %s", reason.c_str());
            doc["error"] = reason;
        }
        std::string out;
        serializeJson(doc, out);
        request->send(code, "application/json", out.c_str());
    }

    void WebServer::handleWiFiConnect(AsyncWebServerRequest *request, JsonVariant &json)
    {
        // It drops the current network for up to 10 s: never while the roof
        // moves, a close waits or the close sequence needs N.I.N.A. (spec 010).
        if (!requireAuth(request) || refuseIfHeld(request, RoofIo::PlatformAction::WifiChange))
            return;

        JsonObject jsonObj = json.as<JsonObject>();
        if (!jsonObj.containsKey("ssid") || !jsonObj.containsKey("password"))
        {
            request->send(400, "application/json", "{\"error\":\"Missing SSID or password\"}");
            return;
        }

        // One attempt at a time: the loop reads the pending network meanwhile.
        if (wifiConnectActive.load())
        {
            request->send(409, "application/json", createErrorJson("A WiFi network is already being tried").c_str());
            return;
        }
        const char *ssid = jsonObj["ssid"];
        const char *password = jsonObj["password"];
        Logger::info(TAG, "Starting nonblocking connection to SSID: '%s'", ssid);

        // Registered under the roof's lock: a close decided from here on waits
        // until the device has joined, for at most RoofIo::NETWORK_HOLD_MAX_MS.
        const RoofIo::HoldReason held = PlatformGuard::claim(
            RoofIo::PlatformAction::WifiChange,
            [this]()
            {
                wifiChangeAtMs = millis();
                wifiChanging = true;
            });
        if (held != RoofIo::HoldReason::None)
        {
            request->send(409, "application/json", createErrorJson(RoofIo::holdText(held)).c_str());
            return;
        }
        pendingWifiSSID = ssid;
        pendingWifiPassword = password;
        wifiConnectConfigSaved = false;
        wifiConnectStartedAt = millis();
        WiFi.disconnect(false);
        WiFi.begin(pendingWifiSSID.c_str(), pendingWifiPassword.c_str());
        // Last: the loop only looks once the old network is dropped and the
        // pending one is set.
        wifiConnectActive = true;

        StaticJsonDocument<256> doc;
        doc["success"] = true;
        doc["pending"] = true;
        doc["message"] = "Connection started";
        String responseStr;
        serializeJson(doc, responseStr);
        request->send(202, "application/json", responseStr.c_str());
    }

    void WebServer::handleGetStatus(AsyncWebServerRequest *request)
    {
        std::string json = createStatusJson();
        request->send(200, "application/json", json.c_str());
    }

    void WebServer::handleGetConfig(AsyncWebServerRequest *request)
    {
        // Spec 021: the revision first, so a save in between only makes the
        // page's copy look older, never newer.
        const uint32_t revision = configRevision.load();
        const Config &cfg = getConfigCallback();
        std::string json = cfg.toJson(true);
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json.c_str());
        response->addHeader("X-Config-Revision", String(revision));
        request->send(response);
    }

    void WebServer::handleRestart(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request) || refuseIfHeld(request, RoofIo::PlatformAction::Restart))
            return;
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
        scheduleRestart(500);
    }

    void WebServer::handleWiFiScan(AsyncWebServerRequest *request)
    {
        // Starting a radio scan is an action, like joining a network
        // (/api/wifi/connect): both need the password when protection is on.
        // A scan takes the radio off channel for about 2 s: a lost N.I.N.A.
        // poll during a close stops it with a fault (spec 010).
        if (!requireAuth(request) || refuseIfHeld(request, RoofIo::PlatformAction::WifiScan))
            return;

        int n = WiFi.scanComplete();

        if (n == WIFI_SCAN_RUNNING)
        {
            request->send(202, "application/json", "{\"success\":true,\"scanning\":true,\"networks\":[]}");
            return;
        }

        if (n < 0)
        {
            WiFi.scanDelete();
            // Registered under the roof's lock; while it runs a close waits.
            const RoofIo::HoldReason held = PlatformGuard::claim(
                RoofIo::PlatformAction::WifiScan,
                [this]()
                {
                    wifiScanAtMs = millis();
                    wifiScanStarting = true;
                });
            if (held != RoofIo::HoldReason::None)
            {
                request->send(409, "application/json", createErrorJson(RoofIo::holdText(held)).c_str());
                return;
            }
            const bool scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
            wifiScanStarting = false;
            if (scanning)
            {
                request->send(202, "application/json", "{\"success\":true,\"scanning\":true,\"networks\":[]}");
            }
            else
            {
                request->send(500, "application/json", createErrorJson("Failed to start WiFi scan").c_str());
            }
            return;
        }

        StaticJsonDocument<2048> doc;
        doc["success"] = true;
        doc["scanning"] = false;
        JsonArray networks = doc.createNestedArray("networks");

        for (int i = 0; i < n; i++)
        {
            JsonObject net = networks.createNestedObject();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
        }

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
        WiFi.scanDelete();
    }

    void WebServer::pollWiFiConnect()
    {
        // The hold on a close is over: drop the flag, so a millis() wrap can't revive it.
        if (wifiChanging.load() && !RoofIo::networkHoldsClose(true, millis() - wifiChangeAtMs.load()))
            wifiChanging = false;
        if (!wifiConnectActive)
        {
            return;
        }

        const wl_status_t status = WiFi.status();
        // Joined the network being tried (not the old one still reported).
        if (status == WL_CONNECTED && pendingWifiSSID == WiFi.SSID().c_str())
        {
            // Joined: the network is back, a waiting close may go (it must
            // not wait for the save below, which waits for it).
            wifiChanging = false;
            if (!wifiConnectConfigSaved)
            {
                // The save writes the flash: never while the roof moves or a
                // close waits (try again next pass; spec 010).
                const PlatformGuard::FlashWrite write;
                if (write.refused() != RoofIo::HoldReason::None)
                    return;
                // Spec 021: onto the live settings under the save lock, so a
                // settings save meanwhile is neither undone nor undoes this.
                wifiConnectConfigSaved = updateConfigCallback(
                    [this](const Config &, Config &next)
                    {
                        next.wifi.ssid = pendingWifiSSID;
                        next.wifi.password = pendingWifiPassword;
                        return true;
                    });
                if (!wifiConnectConfigSaved)
                {
                    Logger::error(TAG, "Connected to WiFi but failed to save config");
                }
            }

            Logger::info(TAG, "WiFi connected. IP: %s", WiFi.localIP().toString().c_str());
            // Restart onto the new network once the setup screen has had time
            // to show the new address.
            if (wifiConnectConfigSaved)
                scheduleRestart(15000);
            // The strings first: the web task may start a new attempt as soon
            // as the flag is clear.
            pendingWifiSSID.clear();
            pendingWifiPassword.clear();
            wifiConnectActive = false;
            return;
        }

        if (millis() - wifiConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS)
        {
            Logger::error(TAG, "WiFi connection timed out. Status: %d", status);
            WiFi.disconnect(false);
            // wifiChanging stays: the saved network is rejoined later; the
            // hold on a close ends at RoofIo::NETWORK_HOLD_MAX_MS anyway.
            pendingWifiSSID.clear();
            pendingWifiPassword.clear();
            wifiConnectActive = false;
        }
    }

    namespace
    {
        // PubSubClient's state() after a failed connect.
        const char *mqttStateText(int state)
        {
            switch (state)
            {
            case -4:
                return "Connection timeout";
            case -3:
                return "Connection lost";
            case -2:
                return "Connect failed";
            case -1:
                return "Disconnected";
            case 1:
                return "Bad protocol";
            case 2:
                return "Bad client ID";
            case 3:
                return "Unavailable";
            case 4:
                return "Bad credentials - check username/password";
            case 5:
                return "Unauthorized";
            default:
                return "Unknown error";
            }
        }
    } // namespace

    void WebServer::handleMQTTTest(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request))
            return;

        JsonObject jsonObj = json.as<JsonObject>();

        if (!jsonObj.containsKey("broker") || !jsonObj.containsKey("port"))
        {
            request->send(400, "application/json", "{\"error\":\"Missing broker or port\"}");
            return;
        }

        const char *broker = jsonObj["broker"];
        uint16_t port = jsonObj["port"];
        const char *username = jsonObj["username"] | "";
        const char *clientId = jsonObj["clientId"] | "RoofRoof-Test";
        // The form shows a saved password as the mask: test with the saved one, but only for the
        // saved broker, port and user.
        const auto passwordToTry =
            Config::mqttTestPassword(getConfigCallback().mqtt, {broker != nullptr ? broker : "", port, username, jsonObj["password"] | ""});
        if (!passwordToTry)
        {
            request->send(
                400, "application/json", createErrorJson("Type the MQTT password to test a broker other than the saved one").c_str());
            return;
        }
        const char *password = passwordToTry->c_str();

        // "[fd00::10]:1883" and bare IPv6 work as in the saved settings (SQMeter spec 015).
        Net::Host host;
        const Net::HostError hostError = Net::parseHost(broker, host);
        if (hostError != Net::HostError::None)
        {
            StaticJsonDocument<192> reply;
            reply["success"] = false;
            reply["error"] = std::string("MQTT broker: ") + Net::hostErrorText(hostError);
            std::string body;
            serializeJson(reply, body);
            request->send(400, "application/json", body.c_str());
            return;
        }
        if (host.port != 0)
            port = host.port;
        Logger::info(TAG, "Testing MQTT connection to %s port %d", host.name.c_str(), port);

        DualStackClient testWifiClient;
        PubSubClient testMqtt(host.name.c_str(), port, testWifiClient);

        // Try to connect with or without auth
        const bool connected = strlen(username) > 0 ? testMqtt.connect(clientId, username, password) : testMqtt.connect(clientId);

        StaticJsonDocument<256> response;

        if (connected)
        {
            Logger::info(TAG, "MQTT test connection successful");
            response["success"] = true;
            response["message"] = "Connection successful";
            testMqtt.disconnect();
        }
        else
        {
            int state = testMqtt.state();
            Logger::error(TAG, "MQTT test connection failed with state: %d", state);

            response["error"] = mqttStateText(state);
            response["state"] = state;
        }

        String responseStr;
        serializeJson(response, responseStr);
        request->send(connected ? 200 : 502, "application/json", responseStr.c_str());
    }

} // namespace SQM
