#include "task_sim_gps.h"
#include "sys_manager.h"
#include <HardwareSerial.h>

// Định nghĩa chân kết nối theo sơ đồ kit TDM2402 & Module SIM A7680C
static const int PIN_SIM_ENABLE = 15; // Kéo HIGH để bật nguồn DC-DC cho module 4G
static const int PIN_SIM_DTR    = 4;  // Kéo LOW để đánh thức module (Sleep/Wake control)
static const int PIN_SIM_TX     = 17; // ESP32 TXD2 -> Module RX
static const int PIN_SIM_RX     = 16; // ESP32 RXD2 <- Module TX
static const int PIN_STATUS_LED = 2;  // LED báo trạng thái

static HardwareSerial SerialSIM(2);

// Helper gửi AT command và chờ phản hồi mong muốn
static bool sendATCmd(const char* cmd, const char* expected, char* respBuf, size_t bufSize, uint32_t timeoutMs) {
    if (respBuf && bufSize > 0) {
        memset(respBuf, 0, bufSize);
    }

    // Xóa buffer nhận cũ
    while (SerialSIM.available()) {
        SerialSIM.read();
    }

    SerialSIM.print(cmd);
    SerialSIM.print("\r\n");

    uint32_t start = millis();
    size_t idx = 0;

    while (millis() - start < timeoutMs) {
        while (SerialSIM.available()) {
            char c = (char)SerialSIM.read();
            if (respBuf && idx < bufSize - 1) {
                respBuf[idx++] = c;
                respBuf[idx] = '\0';
            }
            if (expected && respBuf && strstr(respBuf, expected)) {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return (expected && respBuf) ? (strstr(respBuf, expected) != nullptr) : false;
}

// Hàm phân tích chuỗi phản hồi +CGPSINFO từ A7680C
// Định dạng: +CGPSINFO: <lat>,<N/S>,<lon>,<E/W>,<date>,<UTCtime>,<alt>,<speed>,<course>
// Ví dụ: +CGPSINFO: 2101.5432,N,10548.1234,E,051126,022510.0,15.4,0.0,0.0
// Khi chưa fix: +CGPSINFO: ,,,,,,,,
static bool parseCGPSINFO(const char* buffer, float &outLat, float &outLon, float &outAlt, char* outTime, size_t maxTimeLen) {
    const char* tag = strstr(buffer, "+CGPSINFO:");
    if (!tag) return false;

    const char* p = tag + 10;
    while (*p == ' ') p++;

    // Nếu chưa fix, ký tự đầu tiên sau khoảng trắng là dấu phẩy
    if (*p == ',' || *p == '\0' || *p == '\r' || *p == '\n') {
        return false;
    }

    // Tách 9 trường dữ liệu cách nhau bằng dấu phẩy
    char fields[9][24];
    memset(fields, 0, sizeof(fields));

    int fieldIdx = 0;
    int charIdx = 0;

    while (*p != '\0' && *p != '\r' && *p != '\n' && fieldIdx < 9) {
        if (*p == ',') {
            fields[fieldIdx][charIdx] = '\0';
            fieldIdx++;
            charIdx = 0;
        } else {
            if (charIdx < 23) {
                fields[fieldIdx][charIdx++] = *p;
            }
        }
        p++;
    }
    if (fieldIdx < 9) {
        fields[fieldIdx][charIdx] = '\0';
    }

    // Kiểm tra trường Vĩ độ (field 0) và Kinh độ (field 2)
    if (strlen(fields[0]) < 4 || strlen(fields[2]) < 4) {
        return false;
    }

    // 1. Phân giải Vĩ độ: ddmm.mmmm -> deg + min / 60
    char* dot = strchr(fields[0], '.');
    if (!dot || (dot - fields[0] < 2)) return false;
    int minStart = (dot - fields[0]) - 2;

    char degBuf[8] = {0};
    strncpy(degBuf, fields[0], minStart);
    float latDeg = atof(degBuf);
    float latMin = atof(fields[0] + minStart);
    float lat = latDeg + (latMin / 60.0f);
    if (fields[1][0] == 'S' || fields[1][0] == 's') lat = -lat;

    // 2. Phân giải Kinh độ: dddmm.mmmm -> deg + min / 60
    dot = strchr(fields[2], '.');
    if (!dot || (dot - fields[2] < 2)) return false;
    minStart = (dot - fields[2]) - 2;

    memset(degBuf, 0, sizeof(degBuf));
    strncpy(degBuf, fields[2], minStart);
    float lonDeg = atof(degBuf);
    float lonMin = atof(fields[2] + minStart);
    float lon = lonDeg + (lonMin / 60.0f);
    if (fields[3][0] == 'W' || fields[3][0] == 'w') lon = -lon;

    // 3. Phân giải Cao độ (Altitude)
    float alt = atof(fields[6]);

    // 4. Phân giải Ngày và Giờ (date: DDMMYY, time: HHMMSS.s)
    if (outTime && maxTimeLen > 0) {
        if (strlen(fields[4]) >= 6 && strlen(fields[5]) >= 6) {
            snprintf(outTime, maxTimeLen, "20%c%c-%c%c-%c%c %c%c:%c%c:%c%c",
                     fields[4][4], fields[4][5],  // Năm
                     fields[4][2], fields[4][3],  // Tháng
                     fields[4][0], fields[4][1],  // Ngày
                     fields[5][0], fields[5][1],  // Giờ
                     fields[5][2], fields[5][3],  // Phút
                     fields[5][4], fields[5][5]); // Giây
        } else {
            strncpy(outTime, "Unknown", maxTimeLen - 1);
        }
    }

    outLat = lat;
    outLon = lon;
    outAlt = alt;
    return true;
}

void Task_SIM_A7680C(void *pvParameters) {
    (void)pvParameters;

    // Bước 1: Trì hoãn khởi động 3000ms để hệ thống ESP32 và Wi-Fi AP ổn định dòng nguồn
    vTaskDelay(pdMS_TO_TICKS(3000));
    Serial.println("[SIM GPS] Khoi tao module 4G SIMCom A7680C...");

    // Cấu hình chân DTR và Status LED
    pinMode(PIN_SIM_DTR, OUTPUT);
    digitalWrite(PIN_SIM_DTR, LOW); // Đánh thức module

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    // Kéo chân IO15 lên HIGH để kích hoạt khối nguồn DC-DC cho module 4G
    pinMode(PIN_SIM_ENABLE, OUTPUT);
    digitalWrite(PIN_SIM_ENABLE, HIGH);
    Serial.println("[SIM GPS] Da bat nguon DC-DC cho module SIM (IO15 -> HIGH)");

    // Đợi 4000ms để module nạp firmware và sẵn sàng giao tiếp UART
    vTaskDelay(pdMS_TO_TICKS(4000));

    // Khởi tạo UART2 nối với A7680C
    SerialSIM.begin(115200, SERIAL_8N1, PIN_SIM_RX, PIN_SIM_TX);
    Serial.printf("[SIM GPS] UART2 da mo: Baud 115200, RX=IO%d, TX=IO%d\n", PIN_SIM_RX, PIN_SIM_TX);

    char respBuf[256];
    bool simReady = false;

    // Bước 2: Bắt tay AT với module (Thử tối đa 10 lần)
    for (int retry = 0; retry < 10; retry++) {
        if (sendATCmd("AT", "OK", respBuf, sizeof(respBuf), 1000)) {
            simReady = true;
            Serial.println("[SIM GPS] Module SIMCom A7680C phan hoi AT OK!");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (!simReady) {
        Serial.println("[SIM GPS] CANH BAO: Khong nhan duoc phan hoi AT tu module A7680C!");
        withTelemetry([](GatewayTelemetry &t) {
            t.simReady = false;
            t.gpsFixed = false;
        });
    } else {
        // Tắt echo lệnh
        sendATCmd("ATE0", "OK", respBuf, sizeof(respBuf), 1000);

        // Bật nguồn định vị GNSS/GPS
        // Thử lệnh chuẩn A7680C: AT+CGNSSPWR=1, nếu không hỗ trợ thử AT+CGPS=1
        bool gnssOn = sendATCmd("AT+CGNSSPWR=1", "OK", respBuf, sizeof(respBuf), 2000);
        if (!gnssOn) {
            gnssOn = sendATCmd("AT+CGPS=1", "OK", respBuf, sizeof(respBuf), 2000);
        }

        if (gnssOn) {
            Serial.println("[SIM GPS] Da kich hoat bo thu GNSS/GPS tren A7680C thanh cong.");
        } else {
            Serial.println("[SIM GPS] CGNSSPWR/CGPS khong tra ve OK (co the da bat truoc do).");
        }

        withTelemetry([](GatewayTelemetry &t) {
            t.simReady = true;
            t.gpsFixed = false;
        });
    }

    uint32_t lastLogMs = 0;

    // Bước 3: Vòng lặp định kỳ đọc tọa độ GPS mỗi 5 giây
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Nếu module chưa sẵn sàng, thử bắt tay lại
        if (!simReady) {
            if (sendATCmd("AT", "OK", respBuf, sizeof(respBuf), 1000)) {
                simReady = true;
                sendATCmd("ATE0", "OK", respBuf, sizeof(respBuf), 1000);
                sendATCmd("AT+CGNSSPWR=1", "OK", respBuf, sizeof(respBuf), 2000);
                withTelemetry([](GatewayTelemetry &t) { t.simReady = true; });
                Serial.println("[SIM GPS] Ket noi lai thanh cong toi A7680C!");
            } else {
                continue;
            }
        }

        // Gửi lệnh lấy thông tin GPS
        bool ok = sendATCmd("AT+CGPSINFO", "+CGPSINFO:", respBuf, sizeof(respBuf), 2000);
        if (ok) {
            float lat = 0.0f, lon = 0.0f, alt = 0.0f;
            char timeStr[24] = {0};

            if (parseCGPSINFO(respBuf, lat, lon, alt, timeStr, sizeof(timeStr))) {
                // Nháy LED trạng thái 50ms báo hiệu đã Fix GPS
                digitalWrite(PIN_STATUS_LED, HIGH);
                vTaskDelay(pdMS_TO_TICKS(50));
                digitalWrite(PIN_STATUS_LED, LOW);

                withTelemetry([&](GatewayTelemetry &t) {
                    t.simReady = true;
                    t.gpsFixed = true;
                    t.gpsLatitude = lat;
                    t.gpsLongitude = lon;
                    t.gpsAltitude = alt;
                    strncpy(t.gpsTime, timeStr, sizeof(t.gpsTime) - 1);
                });

                if (millis() - lastLogMs > 30000 || lastLogMs == 0) {
                    Serial.printf("[SIM GPS] Fix thanh cong! Lat: %.6f, Lon: %.6f, Alt: %.1fm, UTC: %s\n",
                                  lat, lon, alt, timeStr);
                    lastLogMs = millis();
                }
            } else {
                // Chưa fix được vị trí (đang dò vệ tinh)
                withTelemetry([](GatewayTelemetry &t) {
                    t.simReady = true;
                    t.gpsFixed = false;
                });

                if (millis() - lastLogMs > 30000 || lastLogMs == 0) {
                    Serial.println("[SIM GPS] Dang do tin hieu ve tinh (No Fix)...");
                    lastLogMs = millis();
                }
            }
        } else {
            // Không phản hồi lệnh CGPSINFO -> có thể module bị treo hoặc mất nguồn
            if (millis() - lastLogMs > 30000) {
                Serial.println("[SIM GPS] Khong nhan duoc phan hoi CGPSINFO tu module.");
                lastLogMs = millis();
            }
        }
    }
}

