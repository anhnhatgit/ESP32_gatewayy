#include "task_serial_cli.h"
#include "sys_manager.h"
#include "task_lora_pf.h"

void CLI_PrintHelp() {
    Serial.println("\n================ [MILESIGHT UG56 CLI COMMANDS] ================");
    Serial.println("  help                   : Hien thi danh sach lenh");
    Serial.println("  status                 : Hien thi thong so he thong va mang chi tiet");
    Serial.println("  wifi <ssid> <pass>     : Cai dat Wi-Fi Station (ket noi Router)");
    Serial.println("  ap <ssid> <pass>       : Cai dat Hotspot Wi-Fi AP phat ra");
    Serial.println("  eui <16_hex>           : Dat ma Gateway EUI");
    Serial.println("  heartbeat <giay>       : Dat chu ky Heartbeat keepalive (vd: 60)");
    Serial.println("  server <addr> <port>   : Dat dia chi Packet Forwarder Server");
    Serial.println("  lora <tan_so> <sf>     : Dat tan so AS923-2 (Hz) va SF (7..12)");
    Serial.println("  milesight list         : Hien thi danh sach cam bien Milesight");
    Serial.println("  milesight test         : Bom goi tin thu nghiem tu cam bien EM300-TH");
    Serial.println("  reboot                 : Khoi dong lai he thong");
    Serial.println("  reset                  : Khoi phuc cai dat goc");
    Serial.println("===============================================================");
}

void CLI_PrintStatus() {
    withTelemetry([&](const GatewayTelemetry &t) {
        Serial.println("\n----------------- [GATEWAY SYSTEM METRICS] -----------------");
        Serial.printf("Device Model : %s (FW %s)\n", GATEWAY_MODEL, GATEWAY_VERSION);
        Serial.printf("Gateway EUI  : %s\n", g_config.gatewayEUI);
        Serial.printf("Uptime       : %lu s | Free Heap: %lu bytes\n", (unsigned long)t.uptimeSec, (unsigned long)t.freeHeapBytes);
        Serial.printf("Network Active: %s\n", t.isEthernetActive ? "Ethernet PoE (Wired)" : "Wi-Fi Station");
        Serial.printf("IP Address   : %s (Hotspot AP: %s - %s)\n", t.currentIP, g_config.apSSID, t.apIP);
        Serial.printf("LoRa Config  : %lu Hz (AS923-2) | SF%u | BW %lu Hz\n",
                      (unsigned long)g_config.loraFreq, g_config.loraSf, (unsigned long)g_config.loraBw);
        Serial.printf("Packets Stat : RX: %lu | Forwarded: %lu\n",
                      (unsigned long)t.rxPktsCount, (unsigned long)t.fwdPktsCount);
        Serial.printf("Server Dest  : %s:%u (Heartbeat: %u s)\n",
                      g_config.serverAddress, g_config.serverPort, g_config.heartbeatIntervalSec);
        Serial.printf("Hardware Env : CPU Temp: %.1f C | PoE: %.1f V | Bat: %.2f V\n",
                      t.chipTempC, t.poeVoltage, t.batteryVoltage);
        Serial.println("------------------------------------------------------------");
    });
}

void CLI_ProcessCommand(const String& cmdLine) {
    String line = cmdLine;
    line.trim();
    if (line.length() == 0) return;

    int firstSpace = line.indexOf(' ');
    String cmd = (firstSpace == -1) ? line : line.substring(0, firstSpace);
    String args = (firstSpace == -1) ? "" : line.substring(firstSpace + 1);
    args.trim();

    if (cmd.equalsIgnoreCase("help")) {
        CLI_PrintHelp();
    }
    else if (cmd.equalsIgnoreCase("status")) {
        CLI_PrintStatus();
    }
    else if (cmd.equalsIgnoreCase("reboot")) {
        SystemManager_Restart(300);
    }
    else if (cmd.equalsIgnoreCase("reset")) {
        SystemManager_ResetDefaults();
        SystemManager_SaveConfig();
        SystemManager_Restart(300);
    }
    else if (cmd.equalsIgnoreCase("wifi")) {
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx > 0) {
            String ssid = args.substring(0, spaceIdx);
            String pass = args.substring(spaceIdx + 1);
            strncpy(g_config.wifiSSID, ssid.c_str(), sizeof(g_config.wifiSSID) - 1);
            strncpy(g_config.wifiPassword, pass.c_str(), sizeof(g_config.wifiPassword) - 1);
            SystemManager_SaveConfig();
            Serial.printf("[CLI] Da cap nhat Wi-Fi STA: %s\n", g_config.wifiSSID);
        } else {
            Serial.println("[CLI] Cu phap: wifi <ssid> <password>");
        }
    }
    else if (cmd.equalsIgnoreCase("ap")) {
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx > 0) {
            String ssid = args.substring(0, spaceIdx);
            String pass = args.substring(spaceIdx + 1);
            strncpy(g_config.apSSID, ssid.c_str(), sizeof(g_config.apSSID) - 1);
            strncpy(g_config.apPassword, pass.c_str(), sizeof(g_config.apPassword) - 1);
            SystemManager_SaveConfig();
            Serial.printf("[CLI] Da cap nhat Wi-Fi Hotspot AP: %s (Khoi dong lai de ap dung)\n", g_config.apSSID);
        } else {
            Serial.println("[CLI] Cu phap: ap <ssid> <password>");
        }
    }
    else if (cmd.equalsIgnoreCase("eui")) {
        if (args.length() == 16) {
            strncpy(g_config.gatewayEUI, args.c_str(), 16);
            g_config.gatewayEUI[16] = '\0';
            SystemManager_SaveConfig();
            Serial.printf("[CLI] Da cap nhat Gateway EUI: %s\n", g_config.gatewayEUI);
        } else {
            Serial.println("[CLI] Cu phap: eui <16_ky_tu_hex>");
        }
    }
    else if (cmd.equalsIgnoreCase("heartbeat")) {
        uint16_t hb = (uint16_t)args.toInt();
        if (hb >= 10) {
            g_config.heartbeatIntervalSec = hb;
            SystemManager_SaveConfig();
            Serial.printf("[CLI] Da cap nhat Heartbeat: %u giay\n", hb);
        } else {
            Serial.println("[CLI] Heartbeat phai toi thieu 10 giay. Cu phap: heartbeat <giay>");
        }
    }
    else if (cmd.equalsIgnoreCase("server")) {
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx > 0) {
            String addr = args.substring(0, spaceIdx);
            uint16_t port = (uint16_t)args.substring(spaceIdx + 1).toInt();
            strncpy(g_config.serverAddress, addr.c_str(), sizeof(g_config.serverAddress) - 1);
            g_config.serverPort = port;
            SystemManager_SaveConfig();
            Serial.printf("[CLI] Da cap nhat Server: %s:%u\n", g_config.serverAddress, g_config.serverPort);
        } else {
            Serial.println("[CLI] Cu phap: server <address> <port>");
        }
    }
    else if (cmd.equalsIgnoreCase("lora")) {
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx > 0) {
            uint32_t freq = (uint32_t)args.substring(0, spaceIdx).toInt();
            uint8_t sf = (uint8_t)args.substring(spaceIdx + 1).toInt();
            g_config.loraFreq = freq;
            g_config.loraSf = sf;
            SystemManager_SaveConfig();
            Serial.printf("[CLI] Da cap nhat LoRa: %lu Hz, SF%u\n", (unsigned long)g_config.loraFreq, g_config.loraSf);
        } else {
            Serial.println("[CLI] Cu phap: lora <tan_so_hz> <sf>");
        }
    }
    else if (cmd.equalsIgnoreCase("milesight")) {
        if (args.equalsIgnoreCase("list")) {
            Serial.println("\n--- [DANH SACH CAM BIEN MILESIGHT DANG QUAN LY] ---");
            for (int i = 0; i < g_config.sensorCount; i++) {
                Serial.printf("[%d] EUI: %s | Ten: %s | Temp: %.1f C | Humi: %.0f%% | Bat: %u%%\n",
                              i + 1,
                              g_config.sensors[i].devEUI,
                              g_config.sensors[i].name,
                              g_config.sensors[i].lastTemp,
                              g_config.sensors[i].lastHumi,
                              g_config.sensors[i].lastBat);
            }
            Serial.println("--------------------------------------------------");
        } else if (args.equalsIgnoreCase("test")) {
            uint8_t em300Payload[] = { 0x01, 0x75, 0x64, 0x03, 0x67, 0xFE, 0x00, 0x04, 0x68, 0x78 };
            LoRa_InjectTestPacket("24E124128C010001", em300Payload, sizeof(em300Payload), -62, 10.5f);
            Serial.println("[CLI] Da bom 1 goi tin thu nghiem tu cam bien EM300-TH (Temp: 25.4C, Humi: 60%, Bat: 100%)!");
        } else {
            Serial.println("[CLI] Cu phap: milesight list HOAC milesight test");
        }
    }
    else {
        Serial.printf("[CLI] Lenh khong xac dinh: '%s'. Go 'help' de xem danh sach lenh.\n", cmd.c_str());
    }
}

void Task_Serial_CLI(void *pvParameters) {
    String inputBuffer = "";
    Serial.println("\n[CLI Task] San sang tiep nhan lenh dieu khien. Go 'help' de xem cu phap.");

    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                CLI_ProcessCommand(inputBuffer);
                inputBuffer = "";
            } else {
                inputBuffer += c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
