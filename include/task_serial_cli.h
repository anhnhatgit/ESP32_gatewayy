#ifndef TASK_SERIAL_CLI_H
#define TASK_SERIAL_CLI_H

#include "system_struct.h"

// Task function đọc tập lệnh qua Serial
void Task_Serial_CLI(void *pvParameters);

// CLI Command Parser
void CLI_ProcessCommand(const String& cmdLine);
void CLI_PrintHelp();
void CLI_PrintStatus();

#endif // TASK_SERIAL_CLI_H

