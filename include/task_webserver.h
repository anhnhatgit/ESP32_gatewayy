#ifndef TASK_WEBSERVER_H
#define TASK_WEBSERVER_H

#include "system_struct.h"

// Task & Service functions
void Task_WebServer(void *pvParameters);
bool WebServer_InitStorage();
void WebServer_RegisterRoutes();

#endif // TASK_WEBSERVER_H

