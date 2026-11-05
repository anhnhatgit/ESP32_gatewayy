#ifndef TASK_LORA_PF_H
#define TASK_LORA_PF_H

#include "system_struct.h"

// Task functions
void Task_LoRa_PacketForwarder(void *pvParameters);
void Task_Heartbeat(void *pvParameters);

// E220-900T30D LoRa Radio APIs
bool LoRa_InitRadio();
bool E220_ApplyConfig();
bool E220_ReadConfig(uint8_t *configBuf);
void LoRa_SendPacket(const uint8_t *payload, size_t size);
void LoRa_InjectTestPacket(const char* devEUI, const uint8_t* payload, size_t size, int16_t rssi, float snr);

#endif // TASK_LORA_PF_H
