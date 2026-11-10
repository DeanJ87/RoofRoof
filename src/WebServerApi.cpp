#include "DualStackClient.h"
#include "Logger.h"
#include "NetAddress.h"
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

    void WebServer::handleSetConfig(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request))
            return;

        String jsonStr;
        serializeJson(json, jsonStr);
        const Config &currentConfig = getConfigCallback();
        std::string reason;
        auto configOpt = Config::fromJson(jsonStr.c_str(), &currentConfig, &reason);
        if (!configOpt)
        {
            Logger::warn(TAG, "Config rejected: %s", reason.c_str());
            request->send(400, "application/json", createErrorJson(reason.empty() ? "Invalid configuration" : reason.c_str()).c_str());
            return;
        }

        if (!saveAndApply(*configOpt))
        {
            request->send(500, "application/json", createErrorJson("Failed to save configuration").c_str());
            return;
        }
        request->send(200, "application/json", "{\"success\":true}");
    }

    void WebServer::handleWiFiConnect(AsyncWebServerRequest *request, JsonVariant &json)
    {
        if (!requireAuth(request))
            return;

        JsonObject jsonObj = json.as<JsonObject>();
        if (!jsonObj.containsKey("ssid") || !jsonObj.containsKey("password"))
        {
            request->send(400, "application/json", "{\"error\":\"Missing SSID or password\"}");
            return;
        }

        const char *ssid = jsonObj["ssid"];
        const char *password = jsonObj["password"];
        Logger::info(TAG, "Starting nonblocking connection to SSID: '%s'", ssid);

        pendingWifiSSID = ssid;
        pendingWifiPassword = password;
        wifiConnectActive = true;
        wifiConnectConfigSaved = false;
        wifiConnectStartedAt = millis();
        WiFi.disconnect(false);
        WiFi.begin(pendingWifiSSID.c_str(), pendingWifiPassword.c_str());

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
        const Config &cfg = getConfigCallback();
        std::string json = cfg.toJson(true);
        request->send(200, "application/json", json.c_str());
    }

    void WebServer::handleRestart(AsyncWebServerRequest *request)
    {
        if (!requireAuth(request))
            return;
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
        scheduleRestart(500);
    }

    void WebServer::handleWiFiScan(AsyncWebServerRequest *request)
    {
        // Starting a radio scan is an action, like joining a network
        // (/api/wifi/connect): both need the password when protection is on.
        if (!requireAuth(request))
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
            if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING)
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
        if (!wifiConnectActive)
        {
            return;
        }

        const wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED)
        {
            if (!wifiConnectConfigSaved)
            {
                Config config = getConfigCallback();
                config.wifi.ssid = pendingWifiSSID;
                config.wifi.password = pendingWifiPassword;
                wifiConnectConfigSaved = saveConfigCallback(config);
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
            wifiConnectActive = false;
            pendingWifiSSID.clear();
            pendingWifiPassword.clear();
            return;
        }

        if (millis() - wifiConnectStartedAt >= WIFI_CONNECT_TIMEOUT_MS)
        {
            Logger::error(TAG, "WiFi connection timed out. Status: %d", status);
            WiFi.disconnect(false);
            wifiConnectActive = false;
            pendingWifiSSID.clear();
            pendingWifiPassword.clear();
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
