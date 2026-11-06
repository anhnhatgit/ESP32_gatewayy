#include "sys_manager.h"
#include "task_sim_gps.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>

// Global Instances
GatewayConfig g_config;
GatewayTelemetry g_telemetry;
SemaphoreHandle_t g_stateMutex = NULL;
QueueHandle_t g_loraRxQueue = NULL;

static Preferences prefs;
static const char* PREF_NAMESPACE = "ug56_cfg";

void updateTelemetry(const GatewayTelemetry& newTelem) {
    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_telemetry = newTelem;
        xSemaphoreGive(g_stateMutex);
    }
}

GatewayTelemetry getTelemetryCopy() {
    GatewayTelemetry copy;
    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        copy = g_telemetry;
        xSemaphoreGive(g_stateMutex);
    }
    return copy;
}

void addLivePacketToTelemetry(const MilesightLivePacket& packet) {
    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Shift cac goi tin cu xuong 1 nac
        int count = g_telemetry.livePacketCount;
        if (count < MAX_LIVE_PACKETS) {
            count++;
            g_telemetry.livePacketCount = count;
        }
        for (int i = count - 1; i > 0; i--) {
            g_telemetry.livePackets[i] = g_telemetry.livePackets[i - 1];
        }
        g_telemetry.livePackets[0] = packet;
        xSemaphoreGive(g_stateMutex);
    }
}

// Bo giai ma Payload chuan Milesight IoT (TLV: Channel + Type + Data)
void parseMilesightPayload(const uint8_t* payload, size_t size, const char* devEUI, int16_t rssi, float snr) {
    float temp = 0.0f;
    float humi = 0.0f;
    uint8_t bat = 100;
    uint16_t co2 = 0;
    float distMm = 0.0f;
    bool hasTemp = false, hasHumi = false, hasBat = false, hasCO2 = false, hasDist = false;

    // Kiểm tra gói tin LoRaWAN Data Up (ChirpStack / standard frame)
    char detectedDevAddr[17] = {0};
    bool isLoRaWAN = false;
    uint16_t lwFCnt = 0;
    uint8_t lwPort = 0;

    if (size >= 12 && (payload[0] == 0x40 || payload[0] == 0x80)) {
        isLoRaWAN = true;
        // DevAddr Little Endian: bytes 4,3,2,1
        snprintf(detectedDevAddr, sizeof(detectedDevAddr), "%02X%02X%02X%02X",
                 payload[4], payload[3], payload[2], payload[1]);
        uint8_t foptsLen = payload[5] & 0x0F;
        lwFCnt = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8);
        if (8 + foptsLen < size - 4) {
            lwPort = payload[8 + foptsLen];
        }
    }

    size_t i = 0;
    while (i < size) {
        uint8_t channel = payload[i++];
        (void)channel;
        if (i >= size) break;
        uint8_t type = payload[i++];

        // IPSO / Milesight Type decoding
        if (type == 0x75 && i < size) { // Battery (%)
            bat = payload[i++];
            hasBat = true;
        } else if (type == 0x67 && i + 1 < size) { // Temperature (0.1 C)
            int16_t rawTemp = (int16_t)(payload[i] | (payload[i + 1] << 8));
            temp = rawTemp / 10.0f;
            i += 2;
            hasTemp = true;
        } else if (type == 0x68 && i < size) { // Humidity (0.5 %RH)
            humi = payload[i++] * 0.5f;
            hasHumi = true;
        } else if (type == 0x7D && i + 1 < size) { // CO2 (ppm)
            co2 = (uint16_t)(payload[i] | (payload[i + 1] << 8));
            i += 2;
            hasCO2 = true;
        } else if (type == 0x82 && i + 1 < size) { // Distance (mm)
            distMm = (float)(payload[i] | (payload[i + 1] << 8));
            i += 2;
            hasDist = true;
        } else {
            // Byte du thua hoac type chua ho tro
            i++;
        }
    }

    // Tao chuoi tom tat giai ma
    String summary = "";
    if (isLoRaWAN) {
        summary = "LoRaWAN [DevAddr: " + String(detectedDevAddr) + ", FCnt: " + String(lwFCnt) + ", Port: " + String(lwPort) + "]";
    } else {
        if (hasTemp) summary += "Temp: " + String(temp, 1) + "°C ";
        if (hasHumi) summary += "| Humi: " + String(humi, 0) + "% ";
        if (hasCO2)  summary += "| CO2: " + String(co2) + "ppm ";
        if (hasDist) summary += "| Dist: " + String(distMm, 0) + "mm ";
        if (hasBat)  summary += "| Bat: " + String(bat) + "%";

        if (summary.length() == 0) summary = "LoRa Uplink RAW (" + String(size) + " bytes)";
    }

    // Chuyen hex string
    char hexStr[64] = {0};
    size_t hexLen = (size < 16) ? size : 16;
    for (size_t k = 0; k < hexLen; k++) {
        sprintf(&hexStr[k * 2], "%02X", payload[k]);
    }

    MilesightLivePacket livePkt;
    livePkt.timestampSec = millis() / 1000;
    const char* targetDevEUI = (isLoRaWAN && detectedDevAddr[0]) ? detectedDevAddr : devEUI;
    strncpy(livePkt.devEUI, targetDevEUI, sizeof(livePkt.devEUI) - 1);
    strncpy(livePkt.modelName, isLoRaWAN ? "LoRaWAN Sensor" : "Milesight Sensor", sizeof(livePkt.modelName) - 1);
    strncpy(livePkt.rawPayloadHex, hexStr, sizeof(livePkt.rawPayloadHex) - 1);
    strncpy(livePkt.decodedSummary, summary.c_str(), sizeof(livePkt.decodedSummary) - 1);
    livePkt.rssi = rssi;
    livePkt.snr = snr;

    // Cap nhat cam bien da dang ky
    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int s = 0; s < g_config.sensorCount; s++) {
            if (strcasecmp(g_config.sensors[s].devEUI, targetDevEUI) == 0 ||
                strcasecmp(g_config.sensors[s].devEUI, devEUI) == 0) {
                g_config.sensors[s].lastSeenSec = livePkt.timestampSec;
                if (hasTemp) g_config.sensors[s].lastTemp = temp;
                if (hasHumi) g_config.sensors[s].lastHumi = humi;
                if (hasBat)  g_config.sensors[s].lastBat = bat;
                if (hasCO2)  g_config.sensors[s].lastCO2 = co2;
                if (hasDist) g_config.sensors[s].lastDistance = distMm;
                g_config.sensors[s].lastRSSI = rssi;
                g_config.sensors[s].lastSNR = snr;
                g_config.sensors[s].isOnline = true;
                strncpy(livePkt.modelName, g_config.sensors[s].name, sizeof(livePkt.modelName) - 1);
                break;
            }
        }
        xSemaphoreGive(g_stateMutex);
    }

    addLivePacketToTelemetry(livePkt);
}

void SystemManager_ResetDefaults() {
    memset(&g_config, 0, sizeof(GatewayConfig));

    strncpy(g_config.deviceName, DEFAULT_HOSTNAME, sizeof(g_config.deviceName) - 1);
    strncpy(g_config.adminPassword, "admin", sizeof(g_config.adminPassword) - 1);

    // Wi-Fi Station mac dinh chua bat (de AP luon hoat dong on dinh cho nguoi dung cau hinh)
    memset(g_config.wifiSSID, 0, sizeof(g_config.wifiSSID));
    memset(g_config.wifiPassword, 0, sizeof(g_config.wifiPassword));
    g_config.wifiFallbackEnable = false;
    g_config.wifiStaticIp = false;
    strncpy(g_config.wifiIp, "192.168.1.180", sizeof(g_config.wifiIp) - 1);
    strncpy(g_config.wifiSubnet, "255.255.255.0", sizeof(g_config.wifiSubnet) - 1);
    strncpy(g_config.wifiGateway, "192.168.1.1", sizeof(g_config.wifiGateway) - 1);
    strncpy(g_config.wifiDns, "8.8.8.8", sizeof(g_config.wifiDns) - 1);

    // Wi-Fi AP Hotspot phuc vu cau hinh
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_config.apSSID, sizeof(g_config.apSSID), "HC001-%02X%02X", mac[4], mac[5]);
    strncpy(g_config.apPassword, DEFAULT_AP_PASS, sizeof(g_config.apPassword) - 1);
    g_config.apEnable = true;
    g_config.apChannel = 1;

    // Ethernet
    g_config.ethEnable = true;
    g_config.ethDhcp = true;
    strncpy(g_config.ethStaticIP, "192.168.1.180", sizeof(g_config.ethStaticIP) - 1);
    strncpy(g_config.ethGateway, "192.168.1.1", sizeof(g_config.ethGateway) - 1);
    strncpy(g_config.ethSubnet, "255.255.255.0", sizeof(g_config.ethSubnet) - 1);

    // LoRa E220-900T30D (UART 30dBm / 1W)
    g_config.e220Channel = 71;     // Freq: 850.125 + 71 = 921.125 MHz (AS923)
    g_config.e220AirRate = 2;      // 2.4 kbps
    g_config.e220BaudRate = 9600;  // 9600 bps UART
    g_config.e220TxPower = 0;      // 30 dBm (1W Max)
    g_config.e220Address = 0x0000;
    g_config.e220NetId = 0x00;
    g_config.e220EnableRSSI = true;
    g_config.e220RxPin = 32;       // ESP32 RX <- E220 TXD (Header J9 kit TDM2402)
    g_config.e220TxPin = 33;       // ESP32 TX -> E220 RXD (Header J9 kit TDM2402)
    g_config.e220AuxPin = 14;      // E220 AUX (Busy/Ready) (Header J9 kit TDM2402)
    g_config.e220M0Pin = 25;       // E220 M0 (Header J9 kit TDM2402)
    g_config.e220M1Pin = 26;       // E220 M1 (Header J9 kit TDM2402)

    // LoRa AS923-2 Legacy params
    g_config.loraFreq = 921125000;
    g_config.loraBw = DEFAULT_LORA_BW;
    g_config.loraSf = DEFAULT_LORA_SF;
    g_config.loraTxPower = 30;

    // MQTT Forwarder (Ket noi EMQX / ChirpStack Server trong mang LAN 192.168.1.140)
    g_config.mqttEnable = true;
    strncpy(g_config.mqttServer, "192.168.1.140", sizeof(g_config.mqttServer) - 1);
    g_config.mqttPort = 1883;
    snprintf(g_config.mqttClientId, sizeof(g_config.mqttClientId), "HC001_%02X%02X", mac[4], mac[5]);
    memset(g_config.mqttUser, 0, sizeof(g_config.mqttUser));
    memset(g_config.mqttPass, 0, sizeof(g_config.mqttPass));
    strncpy(g_config.mqttUplinkTopic, "as923/gateway/%s/event/up", sizeof(g_config.mqttUplinkTopic) - 1);
    strncpy(g_config.mqttHeartbeatTopic, "as923/gateway/%s/event/stats", sizeof(g_config.mqttHeartbeatTopic) - 1);
    strncpy(g_config.mqttDownlinkTopic, "as923/gateway/%s/command/+", sizeof(g_config.mqttDownlinkTopic) - 1);

    // Semtech UDP Server Forwarder
    g_config.udpEnable = true;
    strncpy(g_config.serverAddress, "192.168.1.140", sizeof(g_config.serverAddress) - 1);
    g_config.serverPort = DEFAULT_SERVER_PORT;
    g_config.heartbeatIntervalSec = 10; // Chu ky Heartbeat/Stats 10 giay chuan ChirpStack

    snprintf(g_config.gatewayEUI, sizeof(g_config.gatewayEUI), "%02X%02X%02XFFFF%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Cấu hình WireGuard VPN
    g_config.wgEnable = false;
    strncpy(g_config.wgLocalIP, "10.0.0.2", sizeof(g_config.wgLocalIP) - 1);
    strncpy(g_config.wgSubnetMask, "255.255.255.0", sizeof(g_config.wgSubnetMask) - 1);
    strncpy(g_config.wgGateway, "10.0.0.1", sizeof(g_config.wgGateway) - 1);
    memset(g_config.wgPrivateKey, 0, sizeof(g_config.wgPrivateKey));
    strncpy(g_config.wgEndpointAddress, "vpn.example.com", sizeof(g_config.wgEndpointAddress) - 1);
    g_config.wgEndpointPort = 51820;
    memset(g_config.wgPeerPublicKey, 0, sizeof(g_config.wgPeerPublicKey));

    // Cấu hình Gateway Mesh trong suốt (Mặc định: ROOT Gateway)
    g_config.meshEnable = true;
    g_config.meshRole = MESH_ROLE_ROOT;

    // Cai dat san cam bien mau Heartbeat (theo single-log.json)
    g_config.sensorCount = 2;
    strncpy(g_config.sensors[0].devEUI, "01E6AF2D", sizeof(g_config.sensors[0].devEUI) - 1);
    g_config.sensors[0].devEUI[16] = '\0';
    strncpy(g_config.sensors[0].name, "Heartbeat Sensor (single-log)", sizeof(g_config.sensors[0].name) - 1);
    g_config.sensors[0].model = MS_MODEL_CUSTOM;
    g_config.sensors[0].lastTemp = 28.5f;
    g_config.sensors[0].lastHumi = 65.0f;
    g_config.sensors[0].lastBat = 100;
    g_config.sensors[0].lastRSSI = -55;
    g_config.sensors[0].lastSNR = 15.0f;
    g_config.sensors[0].isOnline = true;

    strncpy(g_config.sensors[1].devEUI, "24E124128C010001", sizeof(g_config.sensors[1].devEUI) - 1);
    g_config.sensors[1].devEUI[16] = '\0';
    strncpy(g_config.sensors[1].name, "Milesight EM300-TH", sizeof(g_config.sensors[1].name) - 1);
    g_config.sensors[1].model = MS_MODEL_EM300_TH;
    g_config.sensors[1].lastTemp = 25.0f;
    g_config.sensors[1].lastHumi = 60.0f;
    g_config.sensors[1].lastBat = 95;
    g_config.sensors[1].lastRSSI = -68;
    g_config.sensors[1].lastSNR = 9.0f;
    g_config.sensors[1].isOnline = true;
}

void SystemManager_LoadConfig() {
    prefs.begin(PREF_NAMESPACE, true);
    size_t len = prefs.getBytes("cfg", &g_config, sizeof(GatewayConfig));
    prefs.end();

    if (len != sizeof(GatewayConfig)) {
        Serial.println("[SYS] Chua co cau hinh NVS, su dung cau hinh mac dinh...");
        SystemManager_ResetDefaults();
        SystemManager_SaveConfig();
    } else {
        Serial.println("[SYS] Da doc cau hinh thanh cong tu NVS.");
        // Cap nhat ten thiet bi sang HC001 neu NVS chua cap nhat
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        if (strncmp(g_config.apSSID, "Milesight-", 10) == 0 || strlen(g_config.apSSID) == 0) {
            snprintf(g_config.apSSID, sizeof(g_config.apSSID), "HC001-%02X%02X", mac[4], mac[5]);
            strncpy(g_config.deviceName, "HC001", sizeof(g_config.deviceName) - 1);
            SystemManager_SaveConfig();
        }
        if (strlen(g_config.adminPassword) == 0) {
            strncpy(g_config.adminPassword, "admin", sizeof(g_config.adminPassword) - 1);
            SystemManager_SaveConfig();
        }
        // Tu dong loai bo SSID dummy cu neu co de giu Hotspot AP hoat dong on dinh
        if (strcmp(g_config.wifiSSID, "Office_WiFi") == 0) {
            memset(g_config.wifiSSID, 0, sizeof(g_config.wifiSSID));
            memset(g_config.wifiPassword, 0, sizeof(g_config.wifiPassword));
            g_config.wifiFallbackEnable = false;
            SystemManager_SaveConfig();
        }
        // Tu dong chuyen cac chan E220 sang bo chan trong tren header J9 kit TDM2402 (tranh xung dot 4G IO16/17 va thieu IO21/22)
        if (g_config.e220RxPin == 16 || g_config.e220M0Pin == 21 || g_config.e220M0Pin == 8 || g_config.e220M1Pin == 9 || g_config.e220M0Pin < 0) {
            g_config.e220RxPin = 32;
            g_config.e220TxPin = 33;
            g_config.e220AuxPin = 14;
            g_config.e220M0Pin = 25;
            g_config.e220M1Pin = 26;
            SystemManager_SaveConfig();
        }
        // Tu dong chuyen dia chi server sang ChirpStack Server mang LAN 192.168.1.140 neu con luu dia chi mac dinh cloud cu
        if (strcmp(g_config.serverAddress, "eu1.cloud.thethings.network") == 0 || strlen(g_config.serverAddress) == 0) {
            strncpy(g_config.serverAddress, "192.168.1.140", sizeof(g_config.serverAddress) - 1);
            SystemManager_SaveConfig();
        }
        if (strcmp(g_config.mqttServer, "broker.emqx.io") == 0 || strlen(g_config.mqttServer) == 0) {
            strncpy(g_config.mqttServer, "192.168.1.140", sizeof(g_config.mqttServer) - 1);
            SystemManager_SaveConfig();
        }
        // Tu dong chuan hoa topic MQTT Forwarder sang prefix as923 chuan ChirpStack Gateway OS
        if (strstr(g_config.mqttUplinkTopic, "as923_2") != nullptr ||
            strncmp(g_config.mqttUplinkTopic, "as923/", 6) != 0 ||
            strstr(g_config.mqttDownlinkTopic, "as923_2") != nullptr ||
            strstr(g_config.mqttDownlinkTopic, "command/down") != nullptr) {
            strncpy(g_config.mqttUplinkTopic, "as923/gateway/%s/event/up", sizeof(g_config.mqttUplinkTopic) - 1);
            strncpy(g_config.mqttHeartbeatTopic, "as923/gateway/%s/event/stats", sizeof(g_config.mqttHeartbeatTopic) - 1);
            strncpy(g_config.mqttDownlinkTopic, "as923/gateway/%s/command/+", sizeof(g_config.mqttDownlinkTopic) - 1);
            SystemManager_SaveConfig();
        }
        // Dong bo chu ky Heartbeat ve 10 giay khop cau hinh tren ChirpStack
        if (g_config.heartbeatIntervalSec == 0 || g_config.heartbeatIntervalSec > 15) {
            g_config.heartbeatIntervalSec = 10;
            SystemManager_SaveConfig();
        }
        // Gia tri mac dinh cho WireGuard neu chua thiet lap
        if (strlen(g_config.wgLocalIP) == 0) {
            strncpy(g_config.wgLocalIP, "10.0.0.2", sizeof(g_config.wgLocalIP) - 1);
            strncpy(g_config.wgSubnetMask, "255.255.255.0", sizeof(g_config.wgSubnetMask) - 1);
            strncpy(g_config.wgGateway, "10.0.0.1", sizeof(g_config.wgGateway) - 1);
            g_config.wgEndpointPort = 51820;
            SystemManager_SaveConfig();
        }
        // Gia tri mac dinh cho Gateway Mesh trong suot
        if (g_config.meshRole > 2) {
            g_config.meshEnable = true;
            g_config.meshRole = MESH_ROLE_ROOT;
            SystemManager_SaveConfig();
        }
    }
}

void SystemManager_SaveConfig() {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putBytes("cfg", &g_config, sizeof(GatewayConfig));
    prefs.end();
    Serial.println("[SYS] Da ghi cau hinh Gateway vao NVS.");
}

void SystemManager_Restart(uint32_t delayMs) {
    Serial.printf("[SYS] He thong se khoi dong lai sau %lu ms...\n", (unsigned long)delayMs);
    delay(delayMs);
    esp_restart();
}

void SystemManager_SetLed(bool on) {
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

void SystemManager_PulseLed(uint32_t pulseMs) {
    int cur = digitalRead(PIN_STATUS_LED);
    digitalWrite(PIN_STATUS_LED, !cur);
    vTaskDelay(pdMS_TO_TICKS(pulseMs));
    digitalWrite(PIN_STATUS_LED, cur);
}

void SystemManager_Init() {
    // Tat tam Brownout Detector de tranh reset khi co sụt áp tức thời
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Kéo chân nguồn khối 4G (IO15-SIM Enable) xuống LOW và đặt các chân giao tiếp SIM về INPUT
    // nhằm cách ly hoàn toàn khối 4G lúc khởi động, triệt tiêu dòng rò ký sinh (parasitic leakage)
    pinMode(15, OUTPUT);
    digitalWrite(15, LOW);
    pinMode(4, INPUT);  // DTR
    pinMode(16, INPUT); // 4G TX -> ESP32 RX
    pinMode(17, INPUT); // ESP32 TX -> 4G RX

    // Cấu hình và bật LED IO2 hiển thị trạng thái hoạt động Gateway (sáng ngay khi cấp nguồn)
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    Serial.begin(115200);
    delay(200);
    Serial.println("\n=======================================================");
    Serial.printf("   %s (LoRaWAN AS923-2 | FW %s)\n", GATEWAY_MODEL, GATEWAY_VERSION);
    Serial.println("=======================================================");

    g_stateMutex = xSemaphoreCreateMutex();
    g_loraRxQueue = xQueueCreate(20, sizeof(LoRaRxPacket));

    memset(&g_telemetry, 0, sizeof(GatewayTelemetry));
    g_telemetry.netState = NET_DISCONNECTED;
    g_telemetry.currentPowerSource = PWR_USB_5V;

    SystemManager_LoadConfig();
    Serial.printf("[SYS] Gateway EUI : %s\n", g_config.gatewayEUI);
    Serial.printf("[SYS] Hotspot AP  : %s (Pass: %s)\n", g_config.apSSID, g_config.apPassword);
    Serial.printf("[SYS] LoRa Channel: %lu Hz (AS923-2), SF%u\n", (unsigned long)g_config.loraFreq, g_config.loraSf);
    const char* meshRoleNames[] = {"Standalone (Standard)", "Root (Border Gateway)", "Relay (Satellite Gateway)"};
    Serial.printf("[SYS] Gateway Mesh: %s (Role: %s)\n", 
                  g_config.meshEnable ? "BAT" : "TAT", 
                  meshRoleNames[g_config.meshRole <= 2 ? g_config.meshRole : 0]);

    // Trì hoãn 500ms cho các tụ lọc nguồn (Bulk Capacitors) trên kit TDM2402 nạp đầy điện thế 3.3V trước khi bật RF
    delay(500);

    // Khoi tao truoc ha tang mang (lwIP & Wi-Fi) voi cong suat phat RF thap (8.5dBm)
    // giup giam dong dinh RF tu ~450mA xuong con ~120mA, ngan chan triet de sup ap nguon tren kit TDM2402
    WiFi.mode(WIFI_AP_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    if (g_config.apEnable) {
        IPAddress apIP(192, 168, 4, 1);
        IPAddress apGateway(192, 168, 4, 1);
        IPAddress apSubnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apGateway, apSubnet);
        WiFi.softAP(g_config.apSSID, g_config.apPassword, g_config.apChannel);
        WiFi.setTxPower(WIFI_POWER_8_5dBm);
        delay(150);
        Serial.printf("[SYS] Hotspot AP da san sang: %s (Pass: %s | IP: %s)\n",
                      g_config.apSSID, g_config.apPassword, WiFi.softAPIP().toString().c_str());
    }

    // KHOI TAO PHAN BO CAC RTOS TASKS VAO CORE 0 & CORE 1
    // Core 0: Ha tang mang Failover, SIM A7680C GNSS & Giao tiep Serial CLI
    xTaskCreatePinnedToCore(Task_Network,    "Task_Network",   STACK_SIZE_NETWORK,    NULL, PRIORITY_NETWORK,    NULL, 0);
    xTaskCreatePinnedToCore(Task_SIM_A7680C, "Task_SIM_GPS",  STACK_SIZE_SIM_GPS,    NULL, PRIORITY_SIM_GPS,    NULL, 0);
    xTaskCreatePinnedToCore(Task_Serial_CLI, "Task_CLI",       STACK_SIZE_SERIAL_CLI, NULL, PRIORITY_SERIAL_CLI, NULL, 0);

    // Core 1: LoRa Radio, WebServer, Heartbeat Keepalive, Sensor ADC
    xTaskCreatePinnedToCore(Task_LoRa_PacketForwarder, "Task_LoRa",  STACK_SIZE_LORA,      NULL, PRIORITY_LORA,      NULL, 1);
    xTaskCreatePinnedToCore(Task_WebServer,            "Task_Web",   STACK_SIZE_WEBSERVER, NULL, PRIORITY_WEBSERVER, NULL, 1);
    xTaskCreatePinnedToCore(Task_Heartbeat,            "Task_HBeat", STACK_SIZE_HEARTBEAT, NULL, PRIORITY_HEARTBEAT, NULL, 1);
    xTaskCreatePinnedToCore(Task_Sensor_Power,         "Task_Power", STACK_SIZE_SENSOR,    NULL, PRIORITY_SENSOR,    NULL, 1);

    Serial.println("[SYS] Toan bo 7 FreeRTOS Tasks da duoc khoi tao thanh cong!");
}
