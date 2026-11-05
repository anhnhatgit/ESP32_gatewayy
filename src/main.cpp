#include <Arduino.h>
#include "sys_manager.h"

void setup() {
    // Toàn bộ quy trình khởi tạo NVS, mạng, RTOS tasks, watchdog được quản lý tập trung
    SystemManager_Init();
}

void loop() {
    // Các công việc thực thi đã được phân bổ thành FreeRTOS tasks
    // Loop nhường tài nguyên cho idle task
    vTaskDelay(pdMS_TO_TICKS(1000));
}
