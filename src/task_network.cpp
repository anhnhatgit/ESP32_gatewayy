#include "task_network.h"
#include <WiFi.h>
#include <time.h>
#include <WireGuard-ESP32.h>
#include "lwip/tcpip.h"

#if __has_include(<ETH.h>)
#include <ETH.h>
#endif

static bool s_ethConnected = false;
static bool s_wifiConnected = false;
static WireGuard s_wg;
static bool s_wgStarted = false;
static bool s_ntpConfigured = false;

// Helper cắt khoảng trắng đầu/cuối trực tiếp trên mảng char tĩnh (Tránh lỗi Dangling Pointer)
static void trimCharArray(char* str) {
    if (!str) return;
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == '\t')) {
        str[--len] = '\0';
    }
    int start = 0;
    while (str[start] != '\0' && (str[start] == ' ' || str[start] == '\r' || str[start] == '\n' || str[start] == '\t')) {
        start++;
    }
    if (start > 0) {
        memmove(str, str + start, len - start + 1);
    }
}

static void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
#if defined(CONFIG_ETH_ENABLED) && (CONFIG_ETH_ENABLED == 1)
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[NET] Ethernet Hardware khoi dong.");
            ETH.setHostname(g_config.deviceName);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[NET] Ethernet Link UP (Da cam cap).");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.printf("[NET] Ethernet nhan IP: %s\n", ETH.localIP().toString().c_str());
            s_ethConnected = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[NET] Ethernet Link DOWN (Rut cap hoac mat tin hieu).");
            s_ethConnected = false;
            break;
#endif
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[NET] Wi-Fi STA nhan IP tu Router: %s\n", WiFi.localIP().toString().c_str());
            s_wifiConnected = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            s_wifiConnected = false;
            break;
        default:
            break;
    }
}

bool Network_IsConnected() {
    return s_ethConnected || s_wifiConnected;
}

String Network_GetIPAddress() {
#if defined(CONFIG_ETH_ENABLED) && (CONFIG_ETH_ENABLED == 1)
    if (s_ethConnected) return ETH.localIP().toString();
#endif
    if (s_wifiConnected) return WiFi.localIP().toString();
    return WiFi.softAPIP().toString();
}

void Task_Network(void *pvParameters) {
    // Trì hoãn 1500ms khi vừa boot để Wi-Fi AP và hệ thống ổn định trước khi quét STA
    vTaskDelay(pdMS_TO_TICKS(1500));
    Serial.println("[NET Task] Tien trinh Failover mang bat dau (ESP32 WROOM)...");
    WiFi.onEvent(WiFiEvent);

    uint8_t wifiRetryCount = 0;
    bool staInitiated = false;

    for (;;) {
        withTelemetry([&](GatewayTelemetry &t) {
            t.isApActive = g_config.apEnable;
            strncpy(t.apIP, WiFi.softAPIP().toString().c_str(), sizeof(t.apIP) - 1);
        });

        // 1. STATE MACHINE LUAN CHUYEN THONG MINH ETHERNET <-> WI-FI
#if defined(CONFIG_ETH_ENABLED) && (CONFIG_ETH_ENABLED == 1)
        if (g_config.ethEnable && s_ethConnected) {
            withTelemetry([&](GatewayTelemetry &t) {
                t.netState = NET_ETH_CONNECTED;
                t.isEthernetActive = true;
                strncpy(t.currentIP, ETH.localIP().toString().c_str(), sizeof(t.currentIP) - 1);
                t.wifiRSSI = 0;
            });

            if (s_wifiConnected) {
                WiFi.disconnect(false);
                s_wifiConnected = false;
                staInitiated = false;
            }
        } 
        else
#endif
        if (g_config.wifiFallbackEnable && (strlen(g_config.wifiSSID) > 0) && (strcmp(g_config.wifiSSID, "Office_WiFi") != 0)) {
            if (WiFi.status() == WL_CONNECTED) {
                if (!s_wifiConnected) {
                    s_wifiConnected = true;
                    Serial.printf("[NET] Wi-Fi STA ket noi thanh cong: %s (IP: %s)\n",
                                  g_config.wifiSSID, WiFi.localIP().toString().c_str());
                }
                wifiRetryCount = 0;
                staInitiated = true;
                withTelemetry([&](GatewayTelemetry &t) {
                    t.netState = NET_WIFI_CONNECTED;
                    t.isEthernetActive = false;
                    strncpy(t.currentIP, WiFi.localIP().toString().c_str(), sizeof(t.currentIP) - 1);
                    t.wifiRSSI = WiFi.RSSI();
                });
            } else {
                s_wifiConnected = false;
                withTelemetry([&](GatewayTelemetry &t) {
                    t.netState = NET_WIFI_CONNECTING;
                    t.isEthernetActive = false;
                    strncpy(t.currentIP, WiFi.softAPIP().toString().c_str(), sizeof(t.currentIP) - 1);
                });

                if (!staInitiated || (++wifiRetryCount >= 12)) {
                    wifiRetryCount = 0;
                    staInitiated = true;

                    if (g_config.wifiStaticIp) {
                        IPAddress ip, gw, mask, dns;
                        if (ip.fromString(g_config.wifiIp) && mask.fromString(g_config.wifiSubnet) && gw.fromString(g_config.wifiGateway)) {
                            if (strlen(g_config.wifiDns) > 0) dns.fromString(g_config.wifiDns);
                            else dns = gw;
                            WiFi.config(ip, gw, mask, dns);
                            Serial.printf("[NET] Wi-Fi STA ap dung IP tinh: %s | GW: %s\n", g_config.wifiIp, g_config.wifiGateway);
                        }
                    } else {
                        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
                    }

                    Serial.printf("[NET Failover] Dang ket noi toi Wi-Fi: %s ...\n", g_config.wifiSSID);
                    WiFi.disconnect(false);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    WiFi.begin(g_config.wifiSSID, g_config.wifiPassword);
                    WiFi.setTxPower(WIFI_POWER_8_5dBm);
                }
            }
        } else {
            s_wifiConnected = false;
            staInitiated = false;
            withTelemetry([&](GatewayTelemetry &t) {
                t.netState = NET_AP_ONLY_MODE;
                t.isEthernetActive = false;
                strncpy(t.currentIP, WiFi.softAPIP().toString().c_str(), sizeof(t.currentIP) - 1);
                t.wifiRSSI = 0;
            });
        }

        // 2. QUẢN LÝ WIREGUARD VPN TUNNEL (An toàn tuyệt đối không gây lỗi PC: 0x00000000)
        bool netOnline = Network_IsConnected();
        if (netOnline) {
            if (!s_ntpConfigured) {
                configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
                s_ntpConfigured = true;
                Serial.println("[NET] Da kich hoat dong bo thoi gian NTP cho WireGuard & System...");
            }

            time_t now = time(nullptr);
            bool timeValid = (now > 1600000000);

            if (g_config.wgEnable) {
                if (!s_wgStarted && timeValid) {
                    trimCharArray(g_config.wgPrivateKey);
                    trimCharArray(g_config.wgPeerPublicKey);
                    trimCharArray(g_config.wgEndpointAddress);
                    trimCharArray(g_config.wgLocalIP);

                    size_t privLen = strlen(g_config.wgPrivateKey);
                    size_t pubLen  = strlen(g_config.wgPeerPublicKey);
                    size_t epLen   = strlen(g_config.wgEndpointAddress);

                    IPAddress localIP;
                    if (!localIP.fromString(g_config.wgLocalIP)) {
                        Serial.println("[WireGuard] LOI: Local IP khong hop le! Tam tat WireGuard.");
                        g_config.wgEnable = false;
                    } 
                    else if (privLen != 44 || pubLen != 44 || epLen == 0) {
                        Serial.printf("[WireGuard] LOI: Key phai dung 44 ky tu Base64 (Priv:%u, Pub:%u). Tam tat WG.\n",
                                      (unsigned int)privLen, (unsigned int)pubLen);
                        g_config.wgEnable = false;
                    } 
                    else {
                        Serial.printf("[WireGuard] Dang khoi tao VPN toi %s:%u (IP: %s)...\n",
                                      g_config.wgEndpointAddress, g_config.wgEndpointPort, g_config.wgLocalIP);

                        vTaskDelay(pdMS_TO_TICKS(300));

                        s_wgStarted = s_wg.begin(
                            localIP,
                            g_config.wgPrivateKey,
                            g_config.wgEndpointAddress,
                            g_config.wgPeerPublicKey,
                            g_config.wgEndpointPort
                        );

                        if (s_wgStarted) {
                            Serial.println("[WireGuard] Ket noi VPN khoi tao thanh cong!");
                        } else {
                            Serial.println("[WireGuard] Khoi tao VPN that bai (Kiem tra Key Base64)!");
                            g_config.wgEnable = false;
                        }
                    }
                }
            } else if (s_wgStarted) {
                Serial.println("[WireGuard] Da tat hoac huy cau hinh WireGuard.");
                s_wg.end();
                s_wgStarted = false;
            }
        } else {
            if (s_wgStarted) {
                s_wg.end();
                s_wgStarted = false;
            }
            s_ntpConfigured = false;
        }

        withTelemetry([&](GatewayTelemetry &t) {
            t.wgConnected = s_wgStarted && s_wg.is_initialized();
        });

        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}