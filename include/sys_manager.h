#ifndef SYS_MANAGER_H
#define SYS_MANAGER_H

#include "system_struct.h"

// ==========================================
// KÍCH THƯỚC STACK CÁC TASK (TỐI ƯU CHO ESP32 WROOM-32)
// ==========================================
#define STACK_SIZE_NETWORK     14336  // 14 KB cho Wi-Fi AP/STA & WireGuard VPN
#define STACK_SIZE_WEBSERVER   14336  // 14 KB cho AsyncWebServer & REST JSON
#define STACK_SIZE_LORA        16384  // 16 KB cho E220 UART, MQTT & UDP Forwarder
#define STACK_SIZE_SENSOR      6144   // 6 KB cho ADC & Cảm biến
#define STACK_SIZE_HEARTBEAT   6144   // 6 KB cho Heartbeat keepalive
#define STACK_SIZE_SERIAL_CLI  6144   // 6 KB cho Dòng lệnh CLI
#define STACK_SIZE_SIM_GPS     6144   // 6 KB cho Module SIM A7680C & GPS GNSS

// Task Priorities
#define PRIORITY_LORA          4  
#define PRIORITY_NETWORK       3  
#define PRIORITY_WEBSERVER     2  
#define PRIORITY_HEARTBEAT     2  
#define PRIORITY_SIM_GPS       2  
#define PRIORITY_SERIAL_CLI    1  
#define PRIORITY_SENSOR        1  

// Chân LED hiển thị trạng thái hoạt động Gateway (IO2 trên ESP32 / TDM2402)
#define PIN_STATUS_LED 2

// Public APIs
void SystemManager_Init();
void SystemManager_LoadConfig();
void SystemManager_SaveConfig();
void SystemManager_ResetDefaults();
void SystemManager_Restart(uint32_t delayMs = 500);
void SystemManager_SetLed(bool on);
void SystemManager_PulseLed(uint32_t pulseMs = 80);

// Task prototypes
void Task_Network(void *pvParameters);
void Task_WebServer(void *pvParameters);
void Task_LoRa_PacketForwarder(void *pvParameters);
void Task_Sensor_Power(void *pvParameters);
void Task_Heartbeat(void *pvParameters);
void Task_Serial_CLI(void *pvParameters);
void Task_SIM_A7680C(void *pvParameters);

#endif // SYS_MANAGER_H