/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <IPAddress.h>
#include <stdint.h>

class WireGuard
{
private:
    bool _is_initialized = false;
    bool _default_route = true;
    bool _custom_allowed = false;
    uint32_t _allowed_ip = 0;
    uint32_t _allowed_mask = 0;
    uint16_t _local_port = 0;
public:
    void setDefaultRoute(bool enabled) { _default_route = enabled; }
    bool defaultRoute() const { return _default_route; }
    void setLocalPort(uint16_t port) { _local_port = port; }
    uint16_t localPort() const { return _local_port; }
    void setAllowedIPs(const IPAddress& network, const IPAddress& mask);
    void clearAllowedIPs();
    bool begin(const IPAddress& localIP, const IPAddress& Subnet, const IPAddress& Gateway, const char* privateKey, const char* remotePeerAddress, const char* remotePeerPublicKey, uint16_t remotePeerPort);
    bool begin(const IPAddress& localIP, const char* privateKey, const char* remotePeerAddress, const char* remotePeerPublicKey, uint16_t remotePeerPort);
    void end();
    bool is_initialized() const { return this->_is_initialized; }
};
