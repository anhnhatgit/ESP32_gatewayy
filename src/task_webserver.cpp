#include "task_webserver.h"
#include "sys_manager.h"
#include "task_lora_pf.h"
#include "logo_data.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

static AsyncWebServer server(80);
static bool s_fsMounted = false;

static const char FALLBACK_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>HC001 Industrial Gateway</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #f4f7f8; color: #0f172a; padding: 40px 20px; text-align: center; }
        .card { background: #ffffff; border: 1px solid #e2e8f0; padding: 36px 30px; border-radius: 10px; max-width: 580px; margin: 40px auto; text-align: left; box-shadow: 0 4px 14px rgba(0, 156, 166, 0.1); }
        h1 { color: #009ca6; font-size: 20px; margin-bottom: 10px; display: flex; align-items: center; justify-content: space-between; }
        p { color: #475569; line-height: 1.6; font-size: 13.5px; margin-bottom: 12px; }
        .badge { background: #059669; color: white; padding: 3px 8px; border-radius: 4px; font-size: 11px; font-weight: bold; }
        .alert-box { background: #fffbeb; border: 1px solid #fde68a; color: #92400e; padding: 12px 16px; border-radius: 6px; font-size: 13px; margin-top: 18px; }
        code { background: #f1f5f9; padding: 2px 6px; border-radius: 4px; font-family: monospace; color: #009ca6; font-weight: bold; }
    </style>
</head>
<body>
    <div class="card">
        <h1>HC001 LoRaWAN Gateway <span class="badge">ONLINE</span></h1>
        <p>Hệ thống Gateway <strong>HC001</strong> đang hoạt động bình thường trên nền tảng FreeRTOS & LoRa E220-900T30D (AS923-2).</p>
        <div class="alert-box">
            <strong>⚠️ Lưu ý về Giao diện Web:</strong><br>
            Bạn cần chạy lệnh <code>pio run -t uploadfs</code> để nạp phân vùng giao diện LittleFS lên vi điều khiển ESP32.
        </div>
    </div>
</body>
</html>
)rawliteral";

bool WebServer_InitStorage() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Khoi tao LittleFS that bai! Chuyen sang Fallback mode.");
        s_fsMounted = false;
        return false;
    }
    s_fsMounted = true;
    Serial.println("[FS] LittleFS da san sang phuc vu giao dien Web.");
    return true;
}

void WebServer_RegisterRoutes() {
    // 1. Phục vụ tài nguyên tĩnh LittleFS (Kèm Header chống cache trình duyệt)
    server.on("/logo.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "image/jpeg", LOGO_JPG_DATA, sizeof(LOGO_JPG_DATA));
        response->addHeader("Cache-Control", "public, max-age=86400");
        request->send(response);
    });

    if (s_fsMounted) {
        server.serveStatic("/style.css", LittleFS, "/style.css").setCacheControl("no-cache, no-store, must-revalidate");
        server.serveStatic("/app.js", LittleFS, "/app.js").setCacheControl("no-cache, no-store, must-revalidate");
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            if (LittleFS.exists("/index.html")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
                response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
                request->send(response);
            } else {
                request->send(200, "text/html", FALLBACK_HTML);
            }
        });
    } else {
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send(200, "text/html", FALLBACK_HTML);
        });
    }

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (s_fsMounted && LittleFS.exists("/index.html")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            request->send(response);
        } else {
            request->send(200, "text/html", FALLBACK_HTML);
        }
    });

    // 2. GET /api/status - Trạng thái Gateway thời gian thực
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;

        doc["model"] = GATEWAY_MODEL;
        doc["version"] = GATEWAY_VERSION;
        doc["sta_ip"] = WiFi.localIP().toString();
        doc["wifi_ssid"] = g_config.wifiSSID;
        doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
        doc["wifi_static"] = g_config.wifiStaticIp;
        doc["wifi_ip"] = g_config.wifiIp;
        doc["gateway_eui"] = g_config.gatewayEUI;

        // Trạng thái E220-900T30D
        doc["e220_channel"] = g_config.e220Channel;
        doc["e220_freq"] = 850.125f + (float)g_config.e220Channel;
        doc["e220_tx_power"] = (g_config.e220TxPower == 0 ? "30 dBm (1W)" : (g_config.e220TxPower == 1 ? "27 dBm" : (g_config.e220TxPower == 2 ? "24 dBm" : "21 dBm")));

        // Trạng thái Forwarders & VPN
        doc["mqtt_enable"] = g_config.mqttEnable;
        doc["udp_enable"] = g_config.udpEnable;
        doc["wg_enable"] = g_config.wgEnable;

        withTelemetry([&](const GatewayTelemetry &t) {
            doc["uptime"] = t.uptimeSec;
            doc["net_state"] = (int)t.netState;
            doc["ip"] = t.currentIP;
            doc["ap_ip"] = t.apIP;
            doc["wifi_rssi"] = t.wifiRSSI;
            doc["is_eth"] = t.isEthernetActive;
            doc["is_ap"] = t.isApActive;
            doc["wg_connected"] = t.wgConnected;

            doc["e220_ready"] = t.e220Ready;
            doc["mqtt_connected"] = t.mqttConnected;

            doc["rx_count"] = t.rxPktsCount;
            doc["tx_count"] = t.txPktsCount;
            doc["fwd_count"] = t.fwdPktsCount;
            doc["last_rssi"] = t.lastRSSI;
            doc["last_snr"] = t.lastSNR;

            doc["temp_c"] = t.chipTempC;
            doc["poe_v"] = t.poeVoltage;
            doc["bat_v"] = t.batteryVoltage;
            doc["power_src"] = (int)t.currentPowerSource;
            doc["free_heap"] = t.freeHeapBytes;

            // GPS GNSS & Module SIM A7680C
            doc["sim_ready"] = t.simReady;
            doc["gps_fixed"] = t.gpsFixed;
            doc["gps_lat"] = t.gpsLatitude;
            doc["gps_lon"] = t.gpsLongitude;
            doc["gps_alt"] = t.gpsAltitude;
            doc["gps_time"] = t.gpsTime;

            // 10 gói tin live gần nhất
            JsonArray pkts = doc["live_packets"].to<JsonArray>();
            for (int i = 0; i < t.livePacketCount; i++) {
                JsonObject p = pkts.add<JsonObject>();
                p["time"] = t.livePackets[i].timestampSec;
                p["eui"] = t.livePackets[i].devEUI;
                p["name"] = t.livePackets[i].modelName;
                p["hex"] = t.livePackets[i].rawPayloadHex;
                p["summary"] = t.livePackets[i].decodedSummary;
                p["rssi"] = t.livePackets[i].rssi;
                p["snr"] = t.livePackets[i].snr;
            }
        });

        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // 3. GET /api/config - Xem toàn bộ cấu hình
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["deviceName"] = g_config.deviceName;
        doc["gatewayEUI"] = g_config.gatewayEUI;

        // Wi-Fi
        doc["wifiSSID"] = g_config.wifiSSID;
        doc["wifiStaticIp"] = g_config.wifiStaticIp;
        doc["wifiIp"] = g_config.wifiIp;
        doc["wifiSubnet"] = g_config.wifiSubnet;
        doc["wifiGateway"] = g_config.wifiGateway;
        doc["wifiDns"] = g_config.wifiDns;

        // Hotspot AP
        doc["apSSID"] = g_config.apSSID;
        doc["apEnable"] = g_config.apEnable;

        // LoRa E220-900T30D
        doc["e220Channel"] = g_config.e220Channel;
        doc["e220Freq"] = 850.125f + (float)g_config.e220Channel;
        doc["e220AirRate"] = g_config.e220AirRate;
        doc["e220BaudRate"] = g_config.e220BaudRate;
        doc["e220TxPower"] = g_config.e220TxPower;
        doc["e220Address"] = g_config.e220Address;
        doc["e220NetId"] = g_config.e220NetId;
        doc["e220EnableRSSI"] = g_config.e220EnableRSSI;
        doc["e220RxPin"] = g_config.e220RxPin;
        doc["e220TxPin"] = g_config.e220TxPin;
        doc["e220AuxPin"] = g_config.e220AuxPin;
        doc["e220M0Pin"] = g_config.e220M0Pin;
        doc["e220M1Pin"] = g_config.e220M1Pin;

        // MQTT Forwarder (EMQX & ChirpStack v4)
        doc["mqttEnable"] = g_config.mqttEnable;
        doc["mqttServer"] = g_config.mqttServer;
        doc["mqttPort"] = g_config.mqttPort;
        doc["mqttClientId"] = g_config.mqttClientId;
        doc["mqttUser"] = g_config.mqttUser;
        doc["mqttUplinkTopic"] = g_config.mqttUplinkTopic;
        doc["mqttHeartbeatTopic"] = g_config.mqttHeartbeatTopic;
        doc["mqttDownlinkTopic"] = g_config.mqttDownlinkTopic;

        // Semtech UDP Forwarder
        doc["udpEnable"] = g_config.udpEnable;
        doc["serverAddress"] = g_config.serverAddress;
        doc["serverPort"] = g_config.serverPort;
        doc["heartbeatIntervalSec"] = g_config.heartbeatIntervalSec;

        // WireGuard VPN
        doc["wgEnable"] = g_config.wgEnable;
        doc["wgLocalIP"] = g_config.wgLocalIP;
        doc["wgSubnetMask"] = g_config.wgSubnetMask;
        doc["wgGateway"] = g_config.wgGateway;
        doc["wgPrivateKey"] = g_config.wgPrivateKey;
        doc["wgEndpointAddress"] = g_config.wgEndpointAddress;
        doc["wgEndpointPort"] = g_config.wgEndpointPort;
        doc["wgPeerPublicKey"] = g_config.wgPeerPublicKey;

        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // 4. POST /api/config - Cập nhật cấu hình tổng thể
    server.on(
        "/api/config",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            if (doc["deviceName"].is<const char*>()) strncpy(g_config.deviceName, doc["deviceName"], sizeof(g_config.deviceName) - 1);
            if (doc["gatewayEUI"].is<const char*>()) strncpy(g_config.gatewayEUI, doc["gatewayEUI"], sizeof(g_config.gatewayEUI) - 1);
            
            // Wi-Fi & IP tĩnh
            if (doc["wifiSSID"].is<const char*>()) strncpy(g_config.wifiSSID, doc["wifiSSID"], sizeof(g_config.wifiSSID) - 1);
            if (doc["wifiPassword"].is<const char*>() && strlen(doc["wifiPassword"]) > 0) {
                strncpy(g_config.wifiPassword, doc["wifiPassword"], sizeof(g_config.wifiPassword) - 1);
            }
            if (doc["wifiStaticIp"].is<bool>()) g_config.wifiStaticIp = doc["wifiStaticIp"];
            if (doc["wifiIp"].is<const char*>()) strncpy(g_config.wifiIp, doc["wifiIp"], sizeof(g_config.wifiIp) - 1);
            if (doc["wifiSubnet"].is<const char*>()) strncpy(g_config.wifiSubnet, doc["wifiSubnet"], sizeof(g_config.wifiSubnet) - 1);
            if (doc["wifiGateway"].is<const char*>()) strncpy(g_config.wifiGateway, doc["wifiGateway"], sizeof(g_config.wifiGateway) - 1);
            if (doc["wifiDns"].is<const char*>()) strncpy(g_config.wifiDns, doc["wifiDns"], sizeof(g_config.wifiDns) - 1);

            // E220 LoRa
            bool e220Changed = false;
            if (doc["e220Channel"].is<uint8_t>()) { g_config.e220Channel = doc["e220Channel"]; e220Changed = true; }
            if (doc["e220AirRate"].is<uint8_t>()) { g_config.e220AirRate = doc["e220AirRate"]; e220Changed = true; }
            if (doc["e220BaudRate"].is<uint32_t>()) { g_config.e220BaudRate = doc["e220BaudRate"]; e220Changed = true; }
            if (doc["e220TxPower"].is<uint8_t>()) { g_config.e220TxPower = doc["e220TxPower"]; e220Changed = true; }
            if (doc["e220Address"].is<uint16_t>()) { g_config.e220Address = doc["e220Address"]; e220Changed = true; }
            if (doc["e220NetId"].is<uint8_t>()) { g_config.e220NetId = doc["e220NetId"]; e220Changed = true; }
            if (doc["e220EnableRSSI"].is<bool>()) { g_config.e220EnableRSSI = doc["e220EnableRSSI"]; e220Changed = true; }
            if (doc["e220RxPin"].is<int8_t>()) g_config.e220RxPin = doc["e220RxPin"];
            if (doc["e220TxPin"].is<int8_t>()) g_config.e220TxPin = doc["e220TxPin"];
            if (doc["e220AuxPin"].is<int8_t>()) g_config.e220AuxPin = doc["e220AuxPin"];
            if (doc["e220M0Pin"].is<int8_t>()) g_config.e220M0Pin = doc["e220M0Pin"];
            if (doc["e220M1Pin"].is<int8_t>()) g_config.e220M1Pin = doc["e220M1Pin"];

            // MQTT Forwarder
            if (doc["mqttEnable"].is<bool>()) g_config.mqttEnable = doc["mqttEnable"];
            if (doc["mqttServer"].is<const char*>()) strncpy(g_config.mqttServer, doc["mqttServer"], sizeof(g_config.mqttServer) - 1);
            if (doc["mqttPort"].is<uint16_t>()) g_config.mqttPort = doc["mqttPort"];
            if (doc["mqttClientId"].is<const char*>()) strncpy(g_config.mqttClientId, doc["mqttClientId"], sizeof(g_config.mqttClientId) - 1);
            if (doc["mqttUplinkTopic"].is<const char*>()) strncpy(g_config.mqttUplinkTopic, doc["mqttUplinkTopic"], sizeof(g_config.mqttUplinkTopic) - 1);
            if (doc["mqttHeartbeatTopic"].is<const char*>()) strncpy(g_config.mqttHeartbeatTopic, doc["mqttHeartbeatTopic"], sizeof(g_config.mqttHeartbeatTopic) - 1);
            if (doc["mqttDownlinkTopic"].is<const char*>()) strncpy(g_config.mqttDownlinkTopic, doc["mqttDownlinkTopic"], sizeof(g_config.mqttDownlinkTopic) - 1);
            if (doc["mqttUser"].is<const char*>()) strncpy(g_config.mqttUser, doc["mqttUser"], sizeof(g_config.mqttUser) - 1);
            if (doc["mqttPass"].is<const char*>() && strlen(doc["mqttPass"]) > 0) {
                strncpy(g_config.mqttPass, doc["mqttPass"], sizeof(g_config.mqttPass) - 1);
            }
            if (doc["mqttUplinkTopic"].is<const char*>()) strncpy(g_config.mqttUplinkTopic, doc["mqttUplinkTopic"], sizeof(g_config.mqttUplinkTopic) - 1);
            if (doc["mqttHeartbeatTopic"].is<const char*>()) strncpy(g_config.mqttHeartbeatTopic, doc["mqttHeartbeatTopic"], sizeof(g_config.mqttHeartbeatTopic) - 1);
            if (doc["mqttDownlinkTopic"].is<const char*>()) strncpy(g_config.mqttDownlinkTopic, doc["mqttDownlinkTopic"], sizeof(g_config.mqttDownlinkTopic) - 1);

            // Semtech UDP Server
            if (doc["udpEnable"].is<bool>()) g_config.udpEnable = doc["udpEnable"];
            if (doc["serverAddress"].is<const char*>()) strncpy(g_config.serverAddress, doc["serverAddress"], sizeof(g_config.serverAddress) - 1);
            if (doc["serverPort"].is<uint16_t>()) g_config.serverPort = doc["serverPort"];
            if (doc["heartbeatIntervalSec"].is<uint16_t>()) g_config.heartbeatIntervalSec = doc["heartbeatIntervalSec"];

            // WireGuard VPN
            if (doc["wgEnable"].is<bool>()) g_config.wgEnable = doc["wgEnable"];
            if (doc["wgLocalIP"].is<const char*>()) strncpy(g_config.wgLocalIP, doc["wgLocalIP"], sizeof(g_config.wgLocalIP) - 1);
            if (doc["wgSubnetMask"].is<const char*>()) strncpy(g_config.wgSubnetMask, doc["wgSubnetMask"], sizeof(g_config.wgSubnetMask) - 1);
            if (doc["wgGateway"].is<const char*>()) strncpy(g_config.wgGateway, doc["wgGateway"], sizeof(g_config.wgGateway) - 1);
            if (doc["wgPrivateKey"].is<const char*>()) strncpy(g_config.wgPrivateKey, doc["wgPrivateKey"], sizeof(g_config.wgPrivateKey) - 1);
            if (doc["wgEndpointAddress"].is<const char*>()) strncpy(g_config.wgEndpointAddress, doc["wgEndpointAddress"], sizeof(g_config.wgEndpointAddress) - 1);
            if (doc["wgEndpointPort"].is<uint16_t>()) g_config.wgEndpointPort = doc["wgEndpointPort"];
            if (doc["wgPeerPublicKey"].is<const char*>()) strncpy(g_config.wgPeerPublicKey, doc["wgPeerPublicKey"], sizeof(g_config.wgPeerPublicKey) - 1);

            SystemManager_SaveConfig();

            // Nếu thông số E220 thay đổi, áp dụng ngay lên phần cứng
            if (e220Changed) {
                E220_ApplyConfig();
            }

            request->send(200, "application/json", "{\"success\":true,\"message\":\"Cấu hình đã được lưu thành công!\"}");
        }
    );

    // 5. POST /api/system/generate_eui - Tự sinh Gateway EUI từ địa chỉ MAC
    server.on("/api/system/generate_eui", HTTP_POST, [](AsyncWebServerRequest *request) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(g_config.gatewayEUI, sizeof(g_config.gatewayEUI), "%02X%02X%02XFFFF%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        SystemManager_SaveConfig();

        JsonDocument doc;
        doc["success"] = true;
        doc["gatewayEUI"] = g_config.gatewayEUI;
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // 6. POST /api/lora/e220/apply - Ghi tham số E220-900T30D vào thanh ghi
    server.on("/api/lora/e220/apply", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool ok = E220_ApplyConfig();
        if (ok) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Đã ghi thông số thanh ghi E220-900T30D thành công!\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"error\":\"Module E220 chưa phản hồi lệnh cấu hình!\"}");
        }
    });

    // 7. GET /api/milesight/sensors - Lấy danh sách cảm biến Milesight
    server.on("/api/milesight/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < g_config.sensorCount; i++) {
            JsonObject s = arr.add<JsonObject>();
            s["eui"] = g_config.sensors[i].devEUI;
            s["name"] = g_config.sensors[i].name;
            s["model"] = (int)g_config.sensors[i].model;
            s["last_seen"] = g_config.sensors[i].lastSeenSec;
            s["temp"] = g_config.sensors[i].lastTemp;
            s["humi"] = g_config.sensors[i].lastHumi;
            s["bat"] = g_config.sensors[i].lastBat;
            s["co2"] = g_config.sensors[i].lastCO2;
            s["dist"] = g_config.sensors[i].lastDistance;
            s["rssi"] = g_config.sensors[i].lastRSSI;
            s["snr"] = g_config.sensors[i].lastSNR;
            s["online"] = g_config.sensors[i].isOnline;
        }

        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // 8. POST /api/test/inject_packet - Bơm gói tin cảm biến Heartbeat mẫu (single-log.json)
    server.on("/api/test/inject_packet", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Gói tin LoRaWAN 118 bytes hoàn chỉnh được dựng từ single-log.json:
        // MHDR: 0x40 (UnconfirmedDataUp) | DevAddr: 01e6af2d | FCtrl: 0xC0 | FCnt: 34375 | FPort: 5
        // FRMPayload: 2ca13f3c... | MIC: C7 EC 74 37
        static const uint8_t SINGLE_LOG_HEARTBEAT[118] = {
            0x40, 0x2D, 0xAF, 0xE6, 0x01, 0xC0, 0x47, 0x86, 0x05, 0x2C, 0xA1, 0x3F, 0x3C, 0xB7, 0xF8, 0xCD,
            0x94, 0xC6, 0xA6, 0x4F, 0x99, 0x78, 0x10, 0xFD, 0xBF, 0xBE, 0xE3, 0x91, 0xD2, 0x0F, 0xF8, 0xBC,
            0xBA, 0xCF, 0xF0, 0xAE, 0x1E, 0x70, 0x3C, 0x60, 0x7B, 0x1C, 0x69, 0x36, 0xA2, 0x62, 0x6E, 0xF8,
            0xB9, 0x6C, 0x85, 0x59, 0xB1, 0xCC, 0xCA, 0xFB, 0xDD, 0x7F, 0x75, 0x7C, 0x1A, 0x09, 0xD5, 0x44,
            0x5D, 0xFE, 0xBD, 0x2C, 0x02, 0xB5, 0x8F, 0x56, 0x69, 0xDE, 0x6A, 0xDB, 0xF4, 0x1F, 0x12, 0x77,
            0x3D, 0xE6, 0x8A, 0xA5, 0x29, 0xA7, 0xE6, 0xC4, 0xC0, 0x80, 0x40, 0xA4, 0xF2, 0xD9, 0x35, 0x95,
            0xDB, 0x4E, 0x17, 0x33, 0xB1, 0xF7, 0xF7, 0x67, 0x72, 0x5A, 0x4E, 0x26, 0x52, 0xAD, 0xD7, 0xEE,
            0x39, 0x27, 0xC7, 0xEC, 0x74, 0x37
        };
        LoRa_InjectTestPacket("01E6AF2D", SINGLE_LOG_HEARTBEAT, sizeof(SINGLE_LOG_HEARTBEAT), -55, 15.0f);
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Đã bơm gói tin Heartbeat mẫu (single-log.json) tới EMQX & ChirpStack!\"}");
    });

    // 9. AUTHENTICATION APIs
    server.on(
        "/api/auth/login",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) || !doc["username"].is<const char*>() || !doc["password"].is<const char*>()) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"Dữ liệu không hợp lệ\"}");
                return;
            }
            const char* u = doc["username"];
            const char* p = doc["password"];
            if (strcmp(u, "admin") == 0 && strcmp(p, g_config.adminPassword) == 0) {
                request->send(200, "application/json", "{\"success\":true,\"token\":\"hc001_auth_token_ok\",\"username\":\"admin\"}");
            } else {
                request->send(401, "application/json", "{\"success\":false,\"error\":\"Tên đăng nhập hoặc mật khẩu không chính xác!\"}");
            }
        }
    );

    server.on(
        "/api/auth/change_password",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) || !doc["old_password"].is<const char*>() || !doc["new_password"].is<const char*>()) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"Dữ liệu không hợp lệ\"}");
                return;
            }
            const char* oldP = doc["old_password"];
            const char* newP = doc["new_password"];
            if (strcmp(oldP, g_config.adminPassword) != 0) {
                request->send(401, "application/json", "{\"success\":false,\"error\":\"Mật khẩu hiện tại không đúng!\"}");
                return;
            }
            if (strlen(newP) < 4) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"Mật khẩu mới tối thiểu 4 ký tự!\"}");
                return;
            }
            strncpy(g_config.adminPassword, newP, sizeof(g_config.adminPassword) - 1);
            SystemManager_SaveConfig();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Đổi mật khẩu thành công!\"}");
        }
    );

    // 10. WI-FI SCAN & CONNECT APIs
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanNetworks(false, false);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        if (n > 0) {
            for (int i = 0; i < n && i < 15; i++) {
                if (WiFi.SSID(i).length() == 0) continue;
                JsonObject w = arr.add<JsonObject>();
                w["ssid"] = WiFi.SSID(i);
                w["rssi"] = WiFi.RSSI(i);
                w["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
        }
        WiFi.scanDelete();
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    server.on(
        "/api/wifi/connect",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) || !doc["ssid"].is<const char*>()) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"Tên Wi-Fi (SSID) không được để trống!\"}");
                return;
            }
            const char* ssid = doc["ssid"];
            const char* pass = doc["password"] | "";

            strncpy(g_config.wifiSSID, ssid, sizeof(g_config.wifiSSID) - 1);
            strncpy(g_config.wifiPassword, pass, sizeof(g_config.wifiPassword) - 1);

            if (doc["staticIp"].is<bool>()) g_config.wifiStaticIp = doc["staticIp"];
            if (doc["ip"].is<const char*>()) strncpy(g_config.wifiIp, doc["ip"], sizeof(g_config.wifiIp) - 1);
            if (doc["subnet"].is<const char*>()) strncpy(g_config.wifiSubnet, doc["subnet"], sizeof(g_config.wifiSubnet) - 1);
            if (doc["gateway"].is<const char*>()) strncpy(g_config.wifiGateway, doc["gateway"], sizeof(g_config.wifiGateway) - 1);
            if (doc["dns"].is<const char*>()) strncpy(g_config.wifiDns, doc["dns"], sizeof(g_config.wifiDns) - 1);

            g_config.wifiFallbackEnable = true;
            SystemManager_SaveConfig();

            // Áp dụng IP tĩnh nếu có
            if (g_config.wifiStaticIp) {
                IPAddress ip, gw, mask, dns;
                if (ip.fromString(g_config.wifiIp) && mask.fromString(g_config.wifiSubnet) && gw.fromString(g_config.wifiGateway)) {
                    if (strlen(g_config.wifiDns) > 0) dns.fromString(g_config.wifiDns);
                    else dns = gw;
                    WiFi.config(ip, gw, mask, dns);
                }
            } else {
                WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
            }

            WiFi.disconnect(false);
            vTaskDelay(pdMS_TO_TICKS(100));
            WiFi.begin(g_config.wifiSSID, g_config.wifiPassword);

            Serial.printf("[WEB] Yeu cau ket noi Wi-Fi: %s (IP tinh: %s)\n",
                          g_config.wifiSSID, g_config.wifiStaticIp ? g_config.wifiIp : "DHCP");
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Gateway đang kết nối tới mạng Wi-Fi...\"}");
        }
    );

    // 11. CẬP NHẬT FIRMWARE OTA (Qua Web)
    server.on(
        "/api/system/ota",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool hasError = Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(
                200, "application/json",
                hasError ? "{\"success\":false,\"error\":\"Cập nhật OTA thất bại!\"}"
                         : "{\"success\":true,\"message\":\"Cập nhật OTA thành công! Gateway đang khởi động lại...\"}"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (!hasError) {
                SystemManager_Restart(1500);
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[OTA] Bat dau nap firmware: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Nap firmware hoan tat: %u bytes\n", (unsigned int)(index + len));
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    // 12. SAO LƯU & KHÔI PHỤC CẤU HÌNH (BACKUP / RESTORE NVS JSON)
    server.on("/api/system/backup_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["deviceName"] = g_config.deviceName;
        doc["gatewayEUI"] = g_config.gatewayEUI;
        doc["wifiSSID"] = g_config.wifiSSID;
        doc["wifiPassword"] = g_config.wifiPassword;
        doc["wifiStaticIp"] = g_config.wifiStaticIp;
        doc["wifiIp"] = g_config.wifiIp;
        doc["wifiSubnet"] = g_config.wifiSubnet;
        doc["wifiGateway"] = g_config.wifiGateway;
        doc["wifiDns"] = g_config.wifiDns;

        doc["e220Channel"] = g_config.e220Channel;
        doc["e220AirRate"] = g_config.e220AirRate;
        doc["e220BaudRate"] = g_config.e220BaudRate;
        doc["e220TxPower"] = g_config.e220TxPower;
        doc["e220Address"] = g_config.e220Address;
        doc["e220NetId"] = g_config.e220NetId;
        doc["e220EnableRSSI"] = g_config.e220EnableRSSI;

        doc["mqttEnable"] = g_config.mqttEnable;
        doc["mqttServer"] = g_config.mqttServer;
        doc["mqttPort"] = g_config.mqttPort;
        doc["mqttClientId"] = g_config.mqttClientId;
        doc["mqttUser"] = g_config.mqttUser;
        doc["mqttUplinkTopic"] = g_config.mqttUplinkTopic;
        doc["mqttHeartbeatTopic"] = g_config.mqttHeartbeatTopic;
        doc["mqttDownlinkTopic"] = g_config.mqttDownlinkTopic;

        doc["udpEnable"] = g_config.udpEnable;
        doc["serverAddress"] = g_config.serverAddress;
        doc["serverPort"] = g_config.serverPort;
        doc["heartbeatIntervalSec"] = g_config.heartbeatIntervalSec;

        String jsonStr;
        serializeJsonPretty(doc, jsonStr);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", jsonStr);
        response->addHeader("Content-Disposition", "attachment; filename=\"HC001_backup_config.json\"");
        request->send(response);
    });

    server.on(
        "/api/system/restore_config",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"File cấu hình JSON không hợp lệ!\"}");
                return;
            }

            if (doc["gatewayEUI"].is<const char*>()) strncpy(g_config.gatewayEUI, doc["gatewayEUI"], sizeof(g_config.gatewayEUI) - 1);
            if (doc["wifiSSID"].is<const char*>()) strncpy(g_config.wifiSSID, doc["wifiSSID"], sizeof(g_config.wifiSSID) - 1);
            if (doc["wifiPassword"].is<const char*>()) strncpy(g_config.wifiPassword, doc["wifiPassword"], sizeof(g_config.wifiPassword) - 1);
            if (doc["wifiStaticIp"].is<bool>()) g_config.wifiStaticIp = doc["wifiStaticIp"];
            if (doc["wifiIp"].is<const char*>()) strncpy(g_config.wifiIp, doc["wifiIp"], sizeof(g_config.wifiIp) - 1);
            if (doc["wifiSubnet"].is<const char*>()) strncpy(g_config.wifiSubnet, doc["wifiSubnet"], sizeof(g_config.wifiSubnet) - 1);
            if (doc["wifiGateway"].is<const char*>()) strncpy(g_config.wifiGateway, doc["wifiGateway"], sizeof(g_config.wifiGateway) - 1);

            if (doc["e220Channel"].is<uint8_t>()) g_config.e220Channel = doc["e220Channel"];
            if (doc["e220AirRate"].is<uint8_t>()) g_config.e220AirRate = doc["e220AirRate"];
            if (doc["e220TxPower"].is<uint8_t>()) g_config.e220TxPower = doc["e220TxPower"];

            if (doc["mqttEnable"].is<bool>()) g_config.mqttEnable = doc["mqttEnable"];
            if (doc["mqttServer"].is<const char*>()) strncpy(g_config.mqttServer, doc["mqttServer"], sizeof(g_config.mqttServer) - 1);
            if (doc["mqttPort"].is<uint16_t>()) g_config.mqttPort = doc["mqttPort"];
            if (doc["mqttClientId"].is<const char*>()) strncpy(g_config.mqttClientId, doc["mqttClientId"], sizeof(g_config.mqttClientId) - 1);
            if (doc["mqttUplinkTopic"].is<const char*>()) strncpy(g_config.mqttUplinkTopic, doc["mqttUplinkTopic"], sizeof(g_config.mqttUplinkTopic) - 1);
            if (doc["mqttHeartbeatTopic"].is<const char*>()) strncpy(g_config.mqttHeartbeatTopic, doc["mqttHeartbeatTopic"], sizeof(g_config.mqttHeartbeatTopic) - 1);
            if (doc["mqttDownlinkTopic"].is<const char*>()) strncpy(g_config.mqttDownlinkTopic, doc["mqttDownlinkTopic"], sizeof(g_config.mqttDownlinkTopic) - 1);

            if (doc["udpEnable"].is<bool>()) g_config.udpEnable = doc["udpEnable"];
            if (doc["serverAddress"].is<const char*>()) strncpy(g_config.serverAddress, doc["serverAddress"], sizeof(g_config.serverAddress) - 1);
            if (doc["serverPort"].is<uint16_t>()) g_config.serverPort = doc["serverPort"];

            SystemManager_SaveConfig();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Khôi phục cấu hình thành công! Gateway đang khởi động lại...\"}");
            SystemManager_Restart(1000);
        }
    );

    // 13. POST /api/reboot - Khởi động lại Gateway
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Gateway đang khởi động lại...\"}");
        SystemManager_Restart(800);
    });

    // 14. POST /api/reset_defaults - Khôi phục cài đặt gốc
    server.on("/api/reset_defaults", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Khôi phục cài đặt gốc và khởi động lại...\"}");
        SystemManager_ResetDefaults();
        SystemManager_SaveConfig();
        SystemManager_Restart(1000);
    });

    server.begin();
    Serial.println("[HTTP] AsyncWebServer da san sang tren port 80.");
}

void Task_WebServer(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(300));
    WebServer_InitStorage();
    WebServer_RegisterRoutes();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
