#ifndef SYSTEM_STRUCT_H
#define SYSTEM_STRUCT_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ==========================================
// THÔNG TIN HỆ THỐNG GATEWAY HC001 (MILESIGHT UG56 EQUIVALENT)
// ==========================================
#define GATEWAY_MODEL          "HC001"
#define GATEWAY_VERSION        "2.2.0-PROD"
#define DEFAULT_HOSTNAME       "HC001"
#define DEFAULT_AP_SSID        "HC001-AP"
#define DEFAULT_AP_PASS        "12345678"

// LoRa Tần số AS923-2 (Kênh 1: 921.40 MHz, Kênh 2: 921.60 MHz)
#define DEFAULT_LORA_FREQ_AS923 921400000
#define DEFAULT_LORA_BW        125000
#define DEFAULT_LORA_SF        7
#define DEFAULT_LORA_CR        5
#define DEFAULT_SERVER_PORT    1700
#define DEFAULT_HEARTBEAT_SEC  60

#define MAX_MILESIGHT_SENSORS  16
#define MAX_LIVE_PACKETS       10

// ==========================================
// ENUMS TRẠNG THÁI
// ==========================================
enum NetworkState {
    NET_DISCONNECTED = 0,
    NET_ETH_CONNECTING,
    NET_ETH_CONNECTED,
    NET_WIFI_CONNECTING,
    NET_WIFI_CONNECTED,
    NET_AP_ONLY_MODE
};

enum PowerSource {
    PWR_UNKNOWN = 0,
    PWR_POE,       // Cấp nguồn qua cáp mạng PoE 48V
    PWR_BATTERY,   // Nguồn Pin dự phòng Lithium 3.7V - 4.2V
    PWR_DC_JACK,   // Cắm nguồn DC Adapter 12V
    PWR_USB_5V     // Cấp nguồn qua cổng USB Type-C (5V) / Header +5V
};

enum MilesightModel {
    MS_MODEL_EM300_TH = 0, // Cảm biến Nhiệt độ / Độ ẩm chống nước
    MS_MODEL_EM500_CO2,    // Cảm biến Chất lượng môi trường / CO2
    MS_MODEL_AM103,        // Cảm biến Không khí trong phòng (Temp, Humi, CO2)
    MS_MODEL_WS_PIR,       // Cảm biến Chuyển động / Hiện diện
    MS_MODEL_CUSTOM        // Cảm biến chuẩn LoRaWAN khác
};

// ==========================================
// CẤU TRÚC CẢM BIẾN MILESIGHT (Node Profile)
// ==========================================
struct MilesightSensor {
    char devEUI[17];      // 16 ký tự Hex + null
    char name[32];        // Tên mô tả (vd: "Kho Lạnh 1", "Phòng Server")
    MilesightModel model; // Chủng loại cảm biến
    uint32_t lastSeenSec; // Thời điểm nhận tin gần nhất
    float lastTemp;       // Nhiệt độ (°C)
    float lastHumi;       // Độ ẩm (%RH)
    uint8_t lastBat;      // Phần trăm pin (%)
    uint16_t lastCO2;     // Nồng độ CO2 (ppm)
    float lastDistance;   // Khoảng cách / Mực nước (mm/m)
    int16_t lastRSSI;     // Cường độ sóng
    float lastSNR;        // Tỉ số tín hiệu trên nhiễu
    bool isOnline;        // Đang hoạt động
};

// ==========================================
// GÓI TIN TRỰC TIẾP TỪ CẢM BIẾN MILESIGHT
// ==========================================
struct MilesightLivePacket {
    uint32_t timestampSec;
    char devEUI[17];
    char modelName[20];
    char rawPayloadHex[64];
    char decodedSummary[96];
    int16_t rssi;
    float snr;
};

// ==========================================
// CẤU HÌNH GATEWAY (Persistent Config NVS)
// ==========================================
struct GatewayConfig {
    char deviceName[32];
    char adminPassword[32];

    // Cấu hình Wi-Fi Station
    char wifiSSID[32];
    char wifiPassword[64];
    bool wifiFallbackEnable;

    // Cấu hình Wi-Fi AP (Phát sóng cục bộ)
    bool apEnable;
    char apSSID[32];
    char apPassword[64];
    uint8_t apChannel;

    // Cấu hình Ethernet (PoE)
    bool ethEnable;
    bool ethDhcp;
    char ethStaticIP[16];
    char ethGateway[16];
    char ethSubnet[16];

    // Cấu hình IP tĩnh cho Wi-Fi Station
    bool wifiStaticIp;
    char wifiIp[16];
    char wifiSubnet[16];
    char wifiGateway[16];
    char wifiDns[16];

    // Cấu hình LoRa E220-900T30D (UART)
    uint8_t  e220Channel;     // Kênh tần số (0 - 80). Freq = 850.125 + channel * 1.0 MHz (Kênh 71 = 921.125 MHz)
    uint8_t  e220AirRate;     // Air data rate: 0..7 (0:2.4k, 1:2.4k, 2:2.4k, 3:4.8k, 4:9.6k, 5:19.2k, 6:38.4k, 7:62.5k)
    uint32_t e220BaudRate;    // UART baud rate: 9600, 115200...
    uint8_t  e220TxPower;     // 0: 30dBm (1W), 1: 27dBm, 2: 24dBm, 3: 21dBm
    uint16_t e220Address;     // Địa chỉ module (0x0000 - 0xFFFF)
    uint8_t  e220NetId;       // Network ID (0x00 - 0xFF)
    bool     e220EnableRSSI;  // Bật byte RSSI gói tin từ E220
    int8_t   e220RxPin;       // Chân RX của ESP32 nối TXD của E220 (Mặc định 32 trên kit TDM2402)
    int8_t   e220TxPin;       // Chân TX của ESP32 nối RXD của E220 (Mặc định 33 trên kit TDM2402)
    int8_t   e220AuxPin;      // Chân AUX của E220 (Mặc định 14 trên kit TDM2402)
    int8_t   e220M0Pin;       // Chân M0 của E220 (Mặc định 25 trên kit TDM2402)
    int8_t   e220M1Pin;       // Chân M1 của E220 (Mặc định 26 trên kit TDM2402)

    // Cấu hình MQTT Forwarder (EMQX & ChirpStack v4)
    bool     mqttEnable;
    char     mqttServer[64];
    uint16_t mqttPort;
    char     mqttClientId[32];
    char     mqttUser[32];
    char     mqttPass[32];
    char     mqttUplinkTopic[64];
    char     mqttHeartbeatTopic[64];
    char     mqttDownlinkTopic[64];

    // Cấu hình Semtech UDP & Server
    bool     udpEnable;
    char     serverAddress[64];
    uint16_t serverPort;
    char     gatewayEUI[17];
    uint16_t heartbeatIntervalSec;

    // Cấu hình LoRa AS923-2 (Legacy RF params)
    uint32_t loraFreq;
    uint32_t loraBw;
    uint8_t  loraSf;
    int8_t   loraTxPower;

    // Cấu hình WireGuard VPN
    bool     wgEnable;
    char     wgLocalIP[16];           // IP trong mạng VPN, vd: "10.0.0.2"
    char     wgSubnetMask[16];        // Netmask, vd: "255.255.255.0"
    char     wgGateway[16];           // Gateway, vd: "10.0.0.1"
    char     wgPrivateKey[45];        // Private key Base64 (44 chars + null)
    char     wgEndpointAddress[64];   // Hostname / IP WireGuard Server
    uint16_t wgEndpointPort;          // Cổng UDP (mặc định 51820)
    char     wgPeerPublicKey[45];     // Public key Server Base64 (44 chars + null)

    // Quản lý cảm biến Milesight
    uint8_t sensorCount;
    MilesightSensor sensors[MAX_MILESIGHT_SENSORS];
};

// ==========================================
// TRẠNG THÁI HOẠT ĐỘNG THỜI GIAN THỰC (Telemetry)
// ==========================================
struct GatewayTelemetry {
    // Trạng thái mạng
    NetworkState netState;
    char currentIP[16];
    char apIP[16];
    int8_t wifiRSSI;
    bool isEthernetActive;
    bool isApActive;
    bool wgConnected;

    // Trạng thái Forwarder & Hardware
    bool mqttConnected;
    bool e220Ready;
    float e220FreqMHz;

    // Thống kê LoRa & Forwarder
    uint32_t rxPktsCount;
    uint32_t txPktsCount;
    uint32_t fwdPktsCount;
    uint32_t dropPktsCount;
    int16_t  lastRSSI;
    float    lastSNR;

    // Cảm biến phần cứng nội bộ & Nguồn
    float chipTempC;
    float poeVoltage;
    float batteryVoltage;
    PowerSource currentPowerSource;

    // Trạng thái Module 4G A7680C & Định vị GPS GNSS
    bool     simReady;
    bool     gpsFixed;
    float    gpsLatitude;
    float    gpsLongitude;
    float    gpsAltitude;
    char     gpsTime[24];

    // Hệ thống
    uint32_t uptimeSec;
    uint32_t freeHeapBytes;
    uint32_t minFreeHeapBytes;

    // Bộ nhớ đệm các gói tin Live gần nhất
    uint8_t livePacketCount;
    MilesightLivePacket livePackets[MAX_LIVE_PACKETS];
};

// ==========================================
// GÓI TIN THÔ TRUYỀN GIỮA CÁC TASK
// ==========================================
struct LoRaRxPacket {
    uint8_t payload[256];
    uint16_t size;
    int16_t rssi;
    float snr;
    uint32_t timestampMs;
    char devEUI[17];
};

// ==========================================
// KHAI BÁO BIẾN TOÀN CỤC & ĐỒNG BỘ
// ==========================================
extern GatewayConfig g_config;
extern GatewayTelemetry g_telemetry;
extern SemaphoreHandle_t g_stateMutex;
extern QueueHandle_t g_loraRxQueue;

// Helper đa dụng cập nhật/truy xuất Telemetry trực tiếp không tốn stack memory
template<typename F>
void withTelemetry(F fn) {
    if (g_stateMutex && xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        fn(g_telemetry);
        xSemaphoreGive(g_stateMutex);
    }
}

// Tiện ích thread-safe
void updateTelemetry(const GatewayTelemetry& newTelem);
GatewayTelemetry getTelemetryCopy();
void addLivePacketToTelemetry(const MilesightLivePacket& packet);
void parseMilesightPayload(const uint8_t* payload, size_t size, const char* devEUI, int16_t rssi, float snr);

#endif // SYSTEM_STRUCT_H
