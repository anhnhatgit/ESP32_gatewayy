#ifndef TASK_SENSOR_PWR_H
#define TASK_SENSOR_PWR_H

#include "system_struct.h"

// Task function giám sát nguồn và cảm biến nhiệt độ nội bộ
void Task_Sensor_Power(void *pvParameters);

// Chân ADC đọc nguồn trên ESP32 WROOM (ADC1 Channel: GPIO 34 & GPIO 35)
// Sử dụng ADC1 vì ADC2 bị xung đột khi Wi-Fi hoạt động trên ESP32 chuẩn
#define PIN_ADC_POE_VOLTAGE    34
#define PIN_ADC_BAT_VOLTAGE    35

#endif // TASK_SENSOR_PWR_H
