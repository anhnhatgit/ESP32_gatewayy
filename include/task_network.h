#ifndef TASK_NETWORK_H
#define TASK_NETWORK_H

#include "system_struct.h"

// Task function cho FreeRTOS
void Task_Network(void *pvParameters);

// Tiện ích kiểm tra trạng thái mạng
bool Network_IsConnected();
String Network_GetIPAddress();

#endif // TASK_NETWORK_H

