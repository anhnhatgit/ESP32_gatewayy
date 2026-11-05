#include "task_sensor_pwr.h"
#include "sys_manager.h"
#include "task_network.h"
#include <esp_system.h>

void Task_Sensor_Power(void *pvParameters) {
    Serial.println("[SENSOR Task] Bat dau giam sat nguon, nhiet do va dieu khien LED IO2...");

    // Cau hinh chan ADC
    analogReadResolution(12);

    static uint8_t s_ledCycle = 0;

    for (;;) {
        // 1. Doc nhiet do noi bo ESP32 (Do C)
        float tempC = temperatureRead();

        // 2. Doc dien ap qua cau phan ap (he so dien tro tuy thuoc bo kit phan cung)
        // V_adc = (raw / 4095.0) * 3.3V
        int rawPoe = analogRead(PIN_ADC_POE_VOLTAGE);
        int rawBat = analogRead(PIN_ADC_BAT_VOLTAGE);

        // Gia su he so chia ap: PoE (1:15), Bat (1:2)
        float vPoe = (rawPoe / 4095.0f) * 3.3f * 15.0f;
        float vBat = (rawBat / 4095.0f) * 3.3f * 2.0f;

        // Xac dinh nguon cap hien tai
        PowerSource pwrSrc = PWR_UNKNOWN;
        if (vPoe > 30.0f) {
            pwrSrc = PWR_POE;
        } else if (vBat > 3.0f) {
            pwrSrc = PWR_BATTERY;
        } else {
            pwrSrc = PWR_DC_JACK;
        }

        // 3. Cap nhat Telemetry truc tiep khong ton bo nho stack
        withTelemetry([&](GatewayTelemetry &t) {
            t.chipTempC = tempC;
            t.poeVoltage = vPoe;
            t.batteryVoltage = vBat;
            t.currentPowerSource = pwrSrc;
            t.uptimeSec = millis() / 1000;
            t.freeHeapBytes = esp_get_free_heap_size();
            t.minFreeHeapBytes = esp_get_minimum_free_heap_size();
        });

        // 4. Dieu khien LED IO2 hien thi trang thai hoat dong Gateway:
        // - Khi Gateway hoat dong va DA CO MANG (Ethernet/Wi-Fi):
        //   LED sang on dinh, chop nhip tim (Heartbeat 80ms) moi 2 giay de bao he thong ONLINE.
        // - Khi Gateway hoat dong nhung CHUA CO MANG (Che do AP Hotspot / Dang cho):
        //   LED chop deu 1s sang / 1s tat (500ms ON / 500ms OFF) bao hieu he thong dang cho uplink.
        s_ledCycle++;
        bool netConnected = Network_IsConnected();

        if (netConnected) {
            if (s_ledCycle % 4 == 0) {
                // Nhay nhip tim 80ms moi 2 giay
                digitalWrite(PIN_STATUS_LED, LOW);
                vTaskDelay(pdMS_TO_TICKS(80));
                digitalWrite(PIN_STATUS_LED, HIGH);
                vTaskDelay(pdMS_TO_TICKS(420));
            } else {
                digitalWrite(PIN_STATUS_LED, HIGH);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        } else {
            // Chop deu 1 Hz (500ms ON / 500ms OFF)
            digitalWrite(PIN_STATUS_LED, (s_ledCycle % 2 == 0) ? HIGH : LOW);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

