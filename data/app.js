// ====================================================================
// HC001 INDUSTRIAL LORAWAN GATEWAY CONSOLE APPLICATION JS
// Supports Ebyte E220-900T30D, MQTT, Semtech UDP, Static IP & Web OTA
// ====================================================================

let g_statusInterval = null;
let g_gatewayEUI = "";

// 1. AUTHENTICATION & SESSION MANAGEMENT
function checkAuth() {
    const isAuth = sessionStorage.getItem('hc001_auth');
    const loginScreen = document.getElementById('login-screen');
    const appConsole = document.getElementById('app-console');

    if (isAuth === 'admin') {
        loginScreen.style.display = 'none';
        appConsole.style.display = 'flex';
        initApp();
    } else {
        loginScreen.style.display = 'flex';
        appConsole.style.display = 'none';
        if (g_statusInterval) {
            clearInterval(g_statusInterval);
            g_statusInterval = null;
        }
    }
}

async function handleLogin(e) {
    e.preventDefault();
    const user = document.getElementById('login-user').value.trim();
    const pass = document.getElementById('login-pass').value;
    const errBox = document.getElementById('login-error');
    const submitBtn = document.getElementById('btn-login-submit');

    errBox.style.display = 'none';
    submitBtn.innerText = 'Đang xác thực...';
    submitBtn.disabled = true;

    try {
        const res = await fetch('/api/auth/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username: user, password: pass })
        });
        const data = await res.json();

        if (res.ok && data.success) {
            sessionStorage.setItem('hc001_auth', 'admin');
            checkAuth();
            showToast('Đăng nhập thành công!', 'success');
        } else {
            errBox.innerText = data.error || 'Tên đăng nhập hoặc mật khẩu không chính xác!';
            errBox.style.display = 'block';
        }
    } catch (err) {
        errBox.innerText = 'Không thể kết nối tới Gateway: ' + err.message;
        errBox.style.display = 'block';
    } finally {
        submitBtn.innerText = 'Đăng nhập';
        submitBtn.disabled = false;
    }
}

function handleLogout() {
    sessionStorage.removeItem('hc001_auth');
    location.reload();
}

// 2. TAB NAVIGATION
function switchTab(tabId) {
    document.querySelectorAll('.tab-pane').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));

    const targetTab = document.getElementById('tab-' + tabId);
    if (targetTab) targetTab.classList.add('active');

    const clickedNav = Array.from(document.querySelectorAll('.nav-item')).find(el => el.getAttribute('href') === '#' + tabId);
    if (clickedNav) clickedNav.classList.add('active');

    const titles = {
        'overview': 'Trạng thái tổng quan',
        'wifi-setup': 'Cấu hình Wi-Fi & Mạng',
        'wireguard': 'Cấu hình WireGuard VPN Client',
        'lora-e220': 'Cấu hình Thu/Phát LoRa E220-900T30D',
        'mqtt-fwd': 'Cấu hình MQTT Forwarder',
        'udp-fwd': 'Cấu hình Semtech UDP Forwarder',
        'sensors': 'Quản lý Cảm biến Milesight',
        'maintenance': 'Bảo trì & Cập nhật OTA'
    };
    document.getElementById('page-title').innerText = titles[tabId] || 'Gateway Console';
}

function formatUptime(seconds) {
    if (!seconds || seconds < 0) return '0s';
    const d = Math.floor(seconds / 86400);
    const h = Math.floor((seconds % 86400) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m ${s}s`;
    return `${m}m ${s}s`;
}

// 3. POLLING GATEWAY STATUS & TELEMETRY
async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) return;
        const data = await res.json();

        // Gateway EUI & IP
        g_gatewayEUI = data.gateway_eui || data.eui || '--';
        document.getElementById('hdr-gateway-eui').innerText = g_gatewayEUI;
        if (document.getElementById('val-table-eui') && document.activeElement !== document.getElementById('val-table-eui')) {
            document.getElementById('val-table-eui').value = g_gatewayEUI;
        }

        const isStaConnected = data.wifi_connected && data.sta_ip && data.sta_ip !== '0.0.0.0';
        const activeIp = isStaConnected ? data.sta_ip : (data.ap_ip || '192.168.4.1');
        document.getElementById('hdr-active-ip').innerText = isStaConnected ? `${data.sta_ip} (LAN)` : `${activeIp} (AP)`;

        // Status Pill in Topbar
        const pill = document.getElementById('pill-net-mode');
        const pillText = document.getElementById('pill-net-text');
        if (isStaConnected) {
            pill.className = 'status-pill online';
            pillText.innerText = `LAN: ${data.sta_ip}`;
        } else {
            pill.className = 'status-pill ap-mode';
            pillText.innerText = `Hotspot AP (192.168.4.1)`;
        }

        // Sidebar Footer
        document.getElementById('footer-net-state').innerText = isStaConnected ? `LAN Online: ${data.sta_ip}` : 'Hotspot AP Chỉ định';

        // Overview Metric Cards
        document.getElementById('val-rx-count').innerText = (data.rx_count || 0).toLocaleString();
        document.getElementById('val-fwd-count').innerText = (data.fwd_count || 0).toLocaleString();
        if (data.last_rssi) {
            document.getElementById('val-last-rssi').innerText = `RSSI: ${data.last_rssi} dBm`;
        }

        // E220 LoRa Status
        if (data.e220_freq) {
            document.getElementById('val-e220-freq').innerText = `${data.e220_freq.toFixed(3)} MHz`;
        }
        const e220Badge = document.getElementById('badge-e220-status');
        if (data.e220_ready) {
            e220Badge.innerText = 'E220 Ready (30dBm)';
            e220Badge.className = 'metric-badge success';
        } else {
            e220Badge.innerText = 'E220 Standby';
            e220Badge.className = 'metric-badge info';
        }

        // Forwarder Badges
        document.getElementById('val-mqtt-status').innerText = data.mqtt_enable ? (data.mqtt_connected ? 'Kết nối' : 'Đang nối...') : 'Tắt';
        document.getElementById('val-udp-status').innerText = data.udp_enable ? 'Bật' : 'Tắt';

        // WireGuard Status
        if (document.getElementById('val-table-wg-status')) {
            document.getElementById('val-table-wg-status').innerText = data.wg_enable ? (data.wg_connected ? 'Đang kết nối' : 'Đang chờ mạng...') : 'Tắt';
        }
        if (document.getElementById('disp-wg-status')) {
            const wgEl = document.getElementById('disp-wg-status');
            if (data.wg_enable) {
                if (data.wg_connected) {
                    wgEl.className = 'badge badge-success';
                    wgEl.innerText = 'Đã kết nối (Tunnel UP)';
                } else {
                    wgEl.className = 'badge badge-warning';
                    wgEl.innerText = 'Đang kết nối tới Máy chủ...';
                }
            } else {
                wgEl.className = 'badge badge-secondary';
                wgEl.innerText = 'Đã tắt';
            }
        }

        // Power Status
        const pwrLabels = ["Không rõ", "PoE 48V Active", "Pin dự phòng (Battery)", "Nguồn DC Adapter 12V", "USB Type-C (5V)"];
        const pwrSrc = (data.power_src !== undefined && data.power_src !== null) ? data.power_src : 4;
        document.getElementById('val-pwr-source').innerText = pwrLabels[pwrSrc] || "USB Type-C (5V)";
        if (pwrSrc === 1) {
            document.getElementById('badge-pwr').innerText = "PoE 48V";
            document.getElementById('badge-pwr').className = "metric-badge success";
            document.getElementById('val-pwr-detail').innerText = `Điện áp: ${(data.poe_v || 48.0).toFixed(1)} V`;
        } else if (pwrSrc === 2) {
            document.getElementById('badge-pwr').innerText = "Battery";
            document.getElementById('badge-pwr').className = "metric-badge warning";
            document.getElementById('val-pwr-detail').innerText = `Điện áp Pin: ${(data.bat_v || 3.7).toFixed(2)} V`;
        } else if (pwrSrc === 3) {
            document.getElementById('badge-pwr').innerText = "DC 12V";
            document.getElementById('badge-pwr').className = "metric-badge info";
            document.getElementById('val-pwr-detail').innerText = `Nguồn Adapter DC 12V`;
        } else {
            document.getElementById('badge-pwr').innerText = "USB 5V";
            document.getElementById('badge-pwr').className = "metric-badge info";
            document.getElementById('val-pwr-detail').innerText = `Cổng USB Type-C (5.0V)`;
        }

        // Temp & Heap
        document.getElementById('val-chip-temp').innerText = `${(data.temp_c || 42.0).toFixed(1)} °C`;
        const freeKb = ((data.free_heap || 180000) / 1024).toFixed(1);
        document.getElementById('val-free-heap').innerText = `RAM trống: ${freeKb} KB`;
        document.getElementById('val-uptime').innerText = formatUptime(data.uptime);

        // Hardware & Network Table
        document.getElementById('val-active-iface').innerText = isStaConnected ? 'Wi-Fi Station (Mạng LAN)' : 'Hotspot AP (Trạm phát độc lập)';
        document.getElementById('val-table-lan-ip').innerText = isStaConnected ? data.sta_ip : 'Chưa kết nối (Đang ở AP Mode)';
        document.getElementById('val-table-ip-type').innerText = data.wifi_static ? `IP tĩnh (${data.wifi_ip})` : 'DHCP Tự động';
        document.getElementById('val-table-ap-info').innerText = `${data.is_ap ? 'Phát sóng' : 'Tắt'} (192.168.4.1)`;
        document.getElementById('val-table-wifi-rssi').innerText = isStaConnected ? `${data.wifi_rssi} dBm` : 'N/A';

        // GPS / GNSS Status
        if (document.getElementById('val-table-gps-status')) {
            const gpsEl = document.getElementById('val-table-gps-status');
            if (data.sim_ready) {
                if (data.gps_fixed) {
                    gpsEl.innerHTML = `<span class="badge badge-success">GPS Fix</span> ${Number(data.gps_lat).toFixed(6)}, ${Number(data.gps_lon).toFixed(6)} (${Number(data.gps_alt).toFixed(1)}m)`;
                } else {
                    gpsEl.innerHTML = `<span class="badge badge-warning">A7680C Sẵn sàng</span> Đang dò vệ tinh...`;
                }
            } else {
                gpsEl.innerHTML = `<span class="badge badge-secondary">A7680C Chưa kết nối</span>`;
            }
        }

        // Render Dynamic Banner in Tab 2 (Wi-Fi Setup)
        renderNetworkBanner(isStaConnected, data.wifi_ssid, data.sta_ip);

        // Render Live LoRa Packets
        renderLivePackets(data.live_packets);
    } catch (err) {
        console.warn('Status fetch error:', err);
    }
}

function renderNetworkBanner(isConnected, ssid, staIp) {
    const banner = document.getElementById('banner-network-mode');
    if (!banner) return;

    if (isConnected) {
        banner.className = 'banner-status sta-mode';
        banner.innerHTML = `
            <div>
                <strong>Gateway đã kết nối Wi-Fi thành công!</strong> Tên mạng: <strong>${ssid}</strong> | 
                IP Mạng LAN: <a href="http://${staIp}" target="_blank">http://${staIp}</a>. 
                Bạn có thể mở đường link này từ điện thoại hoặc máy tính trong cùng mạng nội bộ.
            </div>
        `;
    } else {
        banner.className = 'banner-status ap-mode';
        banner.innerHTML = `
            <div>
                <strong>Gateway đang ở chế độ Trạm phát độc lập (Hotspot AP).</strong> 
                Bạn đang kết nối trực tiếp vào Wi-Fi do Gateway phát ra. 
                Vui lòng quét và chọn mạng Wi-Fi bên dưới để cấp Internet cho Gateway và nhận IP mạng LAN.
            </div>
        `;
    }
}

function renderLivePackets(packets) {
    const tbody = document.getElementById('live-packets-body');
    if (!tbody) return;

    if (!packets || packets.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="text-center text-muted">Chưa ghi nhận gói tin LoRa nào. Nhấn "+ Gửi gói tin giả lập" để kiểm tra.</td></tr>';
        return;
    }

    let html = '';
    packets.forEach(p => {
        html += `
            <tr>
                <td>${formatUptime(p.time)}</td>
                <td><code>${p.eui}</code></td>
                <td><strong style="color:var(--primary)">${p.name}</strong></td>
                <td><span style="color:var(--success)">${p.summary}</span></td>
                <td>${p.rssi} dBm / ${(p.snr !== undefined ? p.snr.toFixed(1) : '9.0')} dB</td>
                <td><code style="color:var(--text-muted); font-size:11px;">${p.hex}</code></td>
            </tr>
        `;
    });
    tbody.innerHTML = html;
}

// 4. LOAD CONFIGURATION FROM GATEWAY NVS
async function loadConfig() {
    try {
        const res = await fetch('/api/config');
        if (!res.ok) return;
        const cfg = await res.json();

        // Gateway EUI
        if (cfg.gatewayEUI) {
            g_gatewayEUI = cfg.gatewayEUI;
            document.getElementById('val-table-eui').value = cfg.gatewayEUI;
            document.getElementById('hdr-gateway-eui').innerText = cfg.gatewayEUI;
        }

        // Wi-Fi & Static IP
        if (cfg.wifiSSID) document.getElementById('cfg-wifi-ssid').value = cfg.wifiSSID;
        if (cfg.apSSID) document.getElementById('cfg-ap-ssid').value = cfg.apSSID;
        document.getElementById('cfg-wifi-static').checked = cfg.wifiStaticIp || false;
        if (cfg.wifiIp) document.getElementById('cfg-wifi-ip').value = cfg.wifiIp;
        if (cfg.wifiSubnet) document.getElementById('cfg-wifi-subnet').value = cfg.wifiSubnet;
        if (cfg.wifiGateway) document.getElementById('cfg-wifi-gw').value = cfg.wifiGateway;
        if (cfg.wifiDns) document.getElementById('cfg-wifi-dns').value = cfg.wifiDns;
        toggleStaticIpFields();

        // E220-900T30D
        if (cfg.e220Channel !== undefined) document.getElementById('cfg-e220-channel').value = cfg.e220Channel;
        updateE220FreqDisplay();
        if (cfg.e220AirRate !== undefined) document.getElementById('cfg-e220-air-rate').value = cfg.e220AirRate;
        if (cfg.e220TxPower !== undefined) document.getElementById('cfg-e220-tx-power').value = cfg.e220TxPower;
        if (cfg.e220BaudRate !== undefined) document.getElementById('cfg-e220-baud').value = cfg.e220BaudRate;
        if (cfg.e220Address !== undefined) document.getElementById('cfg-e220-address').value = cfg.e220Address;
        if (cfg.e220NetId !== undefined) document.getElementById('cfg-e220-netid').value = cfg.e220NetId;
        if (cfg.e220EnableRSSI !== undefined) document.getElementById('cfg-e220-enable-rssi').checked = cfg.e220EnableRSSI;
        if (cfg.e220RxPin !== undefined) document.getElementById('cfg-pin-rx').value = cfg.e220RxPin;
        if (cfg.e220TxPin !== undefined) document.getElementById('cfg-pin-tx').value = cfg.e220TxPin;
        if (cfg.e220AuxPin !== undefined) document.getElementById('cfg-pin-aux').value = cfg.e220AuxPin;
        if (cfg.e220M0Pin !== undefined) document.getElementById('cfg-pin-m0').value = cfg.e220M0Pin;
        if (cfg.e220M1Pin !== undefined) document.getElementById('cfg-pin-m1').value = cfg.e220M1Pin;

        // MQTT Forwarder
        document.getElementById('cfg-mqtt-enable').checked = cfg.mqttEnable || false;
        if (cfg.mqttServer) document.getElementById('cfg-mqtt-server').value = cfg.mqttServer;
        if (cfg.mqttPort) document.getElementById('cfg-mqtt-port').value = cfg.mqttPort;
        if (cfg.mqttClientId) document.getElementById('cfg-mqtt-client-id').value = cfg.mqttClientId;
        if (cfg.mqttUser) document.getElementById('cfg-mqtt-user').value = cfg.mqttUser;
        if (cfg.mqttUplinkTopic) document.getElementById('cfg-mqtt-uplink').value = cfg.mqttUplinkTopic;
        if (cfg.mqttHeartbeatTopic) document.getElementById('cfg-mqtt-heartbeat').value = cfg.mqttHeartbeatTopic;
        if (cfg.mqttDownlinkTopic) document.getElementById('cfg-mqtt-downlink').value = cfg.mqttDownlinkTopic;

        // UDP Forwarder
        document.getElementById('cfg-udp-enable').checked = cfg.udpEnable !== undefined ? cfg.udpEnable : true;
        if (cfg.serverAddress) document.getElementById('cfg-server-addr').value = cfg.serverAddress;
        if (cfg.serverPort) document.getElementById('cfg-server-port').value = cfg.serverPort;
        if (cfg.heartbeatIntervalSec) document.getElementById('cfg-heartbeat-sec').value = cfg.heartbeatIntervalSec;

        // WireGuard VPN
        if (document.getElementById('cfg-wg-enable')) {
            document.getElementById('cfg-wg-enable').checked = cfg.wgEnable || false;
            document.getElementById('cfg-wg-local-ip').value = cfg.wgLocalIP || '10.0.0.2';
            document.getElementById('cfg-wg-subnet').value = cfg.wgSubnetMask || '255.255.255.0';
            document.getElementById('cfg-wg-gateway').value = cfg.wgGateway || '10.0.0.1';
            document.getElementById('cfg-wg-endpoint').value = cfg.wgEndpointAddress || '';
            document.getElementById('cfg-wg-port').value = cfg.wgEndpointPort || 51820;
            document.getElementById('cfg-wg-private-key').value = cfg.wgPrivateKey || '';
            document.getElementById('cfg-wg-peer-public-key').value = cfg.wgPeerPublicKey || '';
        }
    } catch (err) {
        console.warn('Config load error:', err);
    }
}

// 5. GATEWAY ID (EUI) AUTO-GENERATE & SAVE
async function generateGatewayEUI() {
    try {
        const res = await fetch('/api/system/generate_eui', { method: 'POST' });
        const data = await res.json();
        if (res.ok && data.success) {
            g_gatewayEUI = data.gatewayEUI;
            document.getElementById('val-table-eui').value = g_gatewayEUI;
            document.getElementById('hdr-gateway-eui').innerText = g_gatewayEUI;
            showToast(`Đã tự sinh Gateway ID: ${g_gatewayEUI}`, 'success');
        } else {
            showToast('Lỗi tự sinh EUI', 'error');
        }
    } catch (err) {
        showToast('Lỗi kết nối: ' + err.message, 'error');
    }
}

async function saveGatewayEUI() {
    const eui = document.getElementById('val-table-eui').value.trim().toUpperCase();
    if (eui.length !== 16) {
        alert('Gateway ID (EUI) phải gồm đúng 16 ký tự Hex (Ví dụ: ACA704FFFFEECE54)!');
        return;
    }

    try {
        const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gatewayEUI: eui })
        });
        if (res.ok) {
            g_gatewayEUI = eui;
            document.getElementById('hdr-gateway-eui').innerText = eui;
            showToast('Gateway ID đã được lưu!', 'success');
            fetchStatus();
        } else {
            showToast('Lỗi lưu Gateway ID', 'error');
        }
    } catch (err) {
        showToast('Lỗi gửi request: ' + err.message, 'error');
    }
}

// 6. WI-FI SITE SURVEY & STATIC IP
function toggleStaticIpFields() {
    const isStatic = document.getElementById('cfg-wifi-static').checked;
    document.getElementById('box-static-ip').style.display = isStatic ? 'block' : 'none';
}

async function scanWifiNetworks() {
    const btn = document.getElementById('btn-scan-wifi');
    const container = document.getElementById('wifi-scan-container');

    btn.innerText = '⏳ Đang quét...';
    btn.disabled = true;
    container.innerHTML = '<div class="p-4 text-center text-muted">Đang quét các mạng Wi-Fi 2.4GHz lân cận...</div>';

    try {
        const res = await fetch('/api/wifi/scan');
        if (!res.ok) throw new Error('Không nhận được kết quả quét');
        const list = await res.json();

        if (!list || list.length === 0) {
            container.innerHTML = '<div class="p-4 text-center text-muted">Không tìm thấy mạng Wi-Fi nào khả dụng xung quanh.</div>';
            return;
        }

        list.sort((a, b) => b.rssi - a.rssi);

        let html = '';
        list.forEach(w => {
            let sigClass = 'sig-1';
            if (w.rssi >= -60) sigClass = 'sig-4';
            else if (w.rssi >= -70) sigClass = 'sig-3';
            else if (w.rssi >= -80) sigClass = 'sig-2';

            const lockIcon = w.secure ? 'WPA2' : 'Mở';
            const safeSSID = w.ssid.replace(/'/g, "\\'");

            html += `
                <div class="wifi-item">
                    <div class="wifi-item-main">
                        <div>
                            <div class="wifi-ssid">${w.ssid}</div>
                            <div class="wifi-security">${lockIcon}</div>
                        </div>
                    </div>
                    <div class="wifi-item-signal">
                        <div class="wifi-signal-bars ${sigClass}">
                            <div class="bar"></div>
                            <div class="bar"></div>
                            <div class="bar"></div>
                            <div class="bar"></div>
                        </div>
                        <div class="wifi-rssi-val">${w.rssi} dBm</div>
                        <button type="button" class="btn btn-sm btn-secondary" onclick="selectWifiNetwork('${safeSSID}')">Chọn</button>
                    </div>
                </div>
            `;
        });
        container.innerHTML = html;
    } catch (err) {
        container.innerHTML = `<div class="p-4 text-center text-danger">Lỗi khi quét mạng Wi-Fi: ${err.message}</div>`;
    } finally {
        btn.innerText = 'Dò quét Wi-Fi';
        btn.disabled = false;
    }
}

function selectWifiNetwork(ssid) {
    const inputSSID = document.getElementById('cfg-wifi-ssid');
    const inputPass = document.getElementById('cfg-wifi-pass');
    inputSSID.value = ssid;
    inputPass.focus();
    showToast(`Đã chọn mạng Wi-Fi: ${ssid}`, 'success');
}

let g_connectPollTimer = null;

async function handleWifiConnect(e) {
    e.preventDefault();
    const ssid = document.getElementById('cfg-wifi-ssid').value.trim();
    const pass = document.getElementById('cfg-wifi-pass').value;
    const isStatic = document.getElementById('cfg-wifi-static').checked;

    const payload = {
        ssid: ssid,
        password: pass,
        staticIp: isStatic
    };

    if (isStatic) {
        payload.ip = document.getElementById('cfg-wifi-ip').value.trim();
        payload.subnet = document.getElementById('cfg-wifi-subnet').value.trim();
        payload.gateway = document.getElementById('cfg-wifi-gw').value.trim();
        payload.dns = document.getElementById('cfg-wifi-dns').value.trim();
    }

    const modal = document.getElementById('modal-connecting');
    const spinner = modal.querySelector('.modal-spinner');
    const title = document.getElementById('modal-title');
    const desc = document.getElementById('modal-desc');
    const successBox = document.getElementById('modal-success-box');

    modal.style.display = 'flex';
    spinner.style.display = 'block';
    successBox.style.display = 'none';
    title.innerText = 'Đang kết nối tới Wi-Fi...';
    desc.innerText = `Đang lưu cấu hình và kết nối tới mạng "${ssid}". Vui lòng đợi trong giây lát...`;

    try {
        const res = await fetch('/api/wifi/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        if (!res.ok || !data.success) throw new Error(data.error || 'Lỗi gửi yêu cầu');

        let attempts = 0;
        if (g_connectPollTimer) clearInterval(g_connectPollTimer);

        g_connectPollTimer = setInterval(async () => {
            attempts++;
            try {
                const sRes = await fetch('/api/status');
                if (sRes.ok) {
                    const sData = await sRes.json();
                    if (sData.wifi_connected && sData.sta_ip && sData.sta_ip !== '0.0.0.0') {
                        clearInterval(g_connectPollTimer);
                        g_connectPollTimer = null;

                        spinner.style.display = 'none';
                        title.innerText = 'Kết nối thành công!';
                        desc.innerText = `Gateway đã nhận địa chỉ IP mạng nội bộ từ Router:`;
                        successBox.style.display = 'block';

                        const lanUrl = `http://${sData.sta_ip}`;
                        const linkEl = document.getElementById('modal-lan-url');
                        linkEl.href = lanUrl;
                        linkEl.innerText = lanUrl;

                        fetchStatus();
                        return;
                    }
                }
            } catch (err) {}

            if (attempts >= 15) {
                clearInterval(g_connectPollTimer);
                g_connectPollTimer = null;
                spinner.style.display = 'none';
                title.innerText = 'Chưa nhận được IP từ Router';
                desc.innerHTML = `Vui lòng kiểm tra lại mật khẩu Wi-Fi hoặc IP tĩnh.<br><small class="text-muted">Hotspot AP vẫn đang phát sóng tại <strong>192.168.4.1</strong>.</small>`;
                successBox.style.display = 'block';
                successBox.querySelector('.success-icon').style.display = 'none';
                successBox.querySelector('h4').style.display = 'none';
                document.querySelector('.new-ip-badge').style.display = 'none';
            }
        }, 2000);
    } catch (err) {
        spinner.style.display = 'none';
        title.innerText = 'Lỗi kết nối';
        desc.innerText = err.message;
    }
}

function closeModal() {
    document.getElementById('modal-connecting').style.display = 'none';
    if (g_connectPollTimer) {
        clearInterval(g_connectPollTimer);
        g_connectPollTimer = null;
    }
    fetchStatus();
}

// 7. LORA E220-900T30D CONFIGURATION
function updateE220FreqDisplay() {
    const ch = parseInt(document.getElementById('cfg-e220-channel').value) || 0;
    const freq = (850.125 + ch * 1.0).toFixed(3);
    document.getElementById('disp-e220-freq').innerText = `${freq} MHz`;
}

async function saveE220Config() {
    const payload = {
        e220Channel: parseInt(document.getElementById('cfg-e220-channel').value),
        e220AirRate: parseInt(document.getElementById('cfg-e220-air-rate').value),
        e220TxPower: parseInt(document.getElementById('cfg-e220-tx-power').value),
        e220BaudRate: parseInt(document.getElementById('cfg-e220-baud').value),
        e220Address: parseInt(document.getElementById('cfg-e220-address').value),
        e220NetId: parseInt(document.getElementById('cfg-e220-netid').value),
        e220EnableRSSI: document.getElementById('cfg-e220-enable-rssi').checked,
        e220RxPin: parseInt(document.getElementById('cfg-pin-rx').value),
        e220TxPin: parseInt(document.getElementById('cfg-pin-tx').value),
        e220AuxPin: parseInt(document.getElementById('cfg-pin-aux').value),
        e220M0Pin: parseInt(document.getElementById('cfg-pin-m0').value),
        e220M1Pin: parseInt(document.getElementById('cfg-pin-m1').value)
    };

    try {
        const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (res.ok) {
            showToast('Đã lưu và ghi tham số vào thanh ghi E220-900T30D!', 'success');
            fetchStatus();
        } else {
            showToast('Lỗi lưu cấu hình E220', 'error');
        }
    } catch (err) {
        showToast('Lỗi gửi request: ' + err.message, 'error');
    }
}

// 8. MQTT FORWARDER CONFIGURATION
async function saveMqttConfig() {
    const payload = {
        mqttEnable: document.getElementById('cfg-mqtt-enable').checked,
        mqttServer: document.getElementById('cfg-mqtt-server').value.trim(),
        mqttPort: parseInt(document.getElementById('cfg-mqtt-port').value),
        mqttClientId: document.getElementById('cfg-mqtt-client-id').value.trim(),
        mqttUser: document.getElementById('cfg-mqtt-user').value.trim(),
        mqttUplinkTopic: document.getElementById('cfg-mqtt-uplink').value.trim(),
        mqttHeartbeatTopic: document.getElementById('cfg-mqtt-heartbeat').value.trim(),
        mqttDownlinkTopic: document.getElementById('cfg-mqtt-downlink').value.trim()
    };

    const pass = document.getElementById('cfg-mqtt-pass').value;
    if (pass.length > 0) payload.mqttPass = pass;

    try {
        const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (res.ok) {
            showToast('Cấu hình MQTT Forwarder đã lưu thành công!', 'success');
            fetchStatus();
        } else {
            showToast('Lỗi lưu cấu hình MQTT', 'error');
        }
    } catch (err) {
        showToast('Lỗi gửi request: ' + err.message, 'error');
    }
}

// 9. UDP FORWARDER CONFIGURATION
async function saveUdpConfig() {
    const payload = {
        udpEnable: document.getElementById('cfg-udp-enable').checked,
        serverAddress: document.getElementById('cfg-server-addr').value.trim(),
        serverPort: parseInt(document.getElementById('cfg-server-port').value),
        heartbeatIntervalSec: parseInt(document.getElementById('cfg-heartbeat-sec').value)
    };

    try {
        const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (res.ok) {
            showToast('Cấu hình Semtech UDP Forwarder đã được lưu!', 'success');
            fetchStatus();
        } else {
            showToast('Lỗi lưu cấu hình UDP', 'error');
        }
    } catch (err) {
        showToast('Lỗi gửi request: ' + err.message, 'error');
    }
}

// 10. SENSORS MANAGEMENT
async function loadSensors() {
    try {
        const res = await fetch('/api/milesight/sensors');
        if (!res.ok) return;
        const list = await res.json();
        const tbody = document.getElementById('sensors-table-body');
        if (!tbody) return;

        const modelNames = ["EM300-TH", "EM500-CO2", "AM103/AM107", "WS Series", "Custom Node"];

        if (!list || list.length === 0) {
            tbody.innerHTML = '<tr><td colspan="7" class="text-center text-muted">Chưa có cảm biến Milesight nào được đồng bộ.</td></tr>';
            return;
        }

        let html = '';
        list.forEach(s => {
            let metricsStr = '';
            if (s.temp !== undefined && s.temp !== 0) metricsStr += `Nhiệt độ: ${s.temp.toFixed(1)}°C `;
            if (s.humi !== undefined && s.humi !== 0) metricsStr += `| Độ ẩm: ${s.humi.toFixed(0)}% `;
            if (s.co2 !== undefined && s.co2 !== 0) metricsStr += `| CO2: ${s.co2}ppm `;
            if (!metricsStr) metricsStr = 'Đang đợi gói tin...';

            html += `
                <tr>
                    <td><strong>${s.name}</strong></td>
                    <td><code>${s.eui}</code></td>
                    <td><span class="badge badge-primary">${modelNames[s.model] || 'Milesight'}</span></td>
                    <td><span class="badge badge-success">Online</span></td>
                    <td><span style="color:var(--text-primary)">${metricsStr}</span></td>
                    <td>${s.bat ? s.bat + '%' : '--'}</td>
                    <td>${s.rssi ? s.rssi + ' dBm' : '--'}</td>
                </tr>
            `;
        });
        tbody.innerHTML = html;
    } catch (err) {
        console.warn('Sensors load error:', err);
    }
}

// 11. OTA FIRMWARE UPDATE
async function handleOtaUpload(e) {
    e.preventDefault();
    const fileInput = document.getElementById('ota-file');
    if (!fileInput.files.length) {
        alert('Vui lòng chọn file firmware (.bin)!');
        return;
    }

    const file = fileInput.files[0];
    const formData = new FormData();
    formData.append('update', file);

    const progressBox = document.getElementById('ota-progress-box');
    const progressBar = document.getElementById('ota-progress-bar');
    const percentText = document.getElementById('ota-percent-text');
    const statusText = document.getElementById('ota-status-text');
    const submitBtn = document.getElementById('btn-ota-submit');

    progressBox.style.display = 'block';
    submitBtn.disabled = true;
    submitBtn.innerText = 'Đang truyền file...';

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/system/ota', true);

    xhr.upload.onprogress = function(evt) {
        if (evt.lengthComputable) {
            const percent = Math.round((evt.loaded / evt.total) * 100);
            progressBar.style.width = percent + '%';
            percentText.innerText = percent + '%';
            if (percent === 100) {
                statusText.innerText = 'Đang ghi Flash và xác minh...';
            }
        }
    };

    xhr.onload = function() {
        if (xhr.status === 200) {
            showToast('Nạp Firmware OTA thành công! Gateway đang khởi động lại...', 'success');
            startRebootCountdown();
        } else {
            showToast('Lỗi nạp OTA: ' + xhr.responseText, 'error');
            submitBtn.disabled = false;
            submitBtn.innerText = 'Bắt đầu nạp OTA';
        }
    };

    xhr.onerror = function() {
        showToast('Lỗi kết nối khi nạp OTA!', 'error');
        submitBtn.disabled = false;
        submitBtn.innerText = 'Bắt đầu nạp OTA';
    };

    xhr.send(formData);
}

// 12. BACKUP & RESTORE CONFIGURATION
async function handleRestoreConfig() {
    const fileInput = document.getElementById('restore-file');
    if (!fileInput.files.length) {
        alert('Vui lòng chọn file backup JSON để khôi phục!');
        return;
    }

    const file = fileInput.files[0];
    const reader = new FileReader();

    reader.onload = async function(evt) {
        try {
            const jsonText = evt.target.result;
            JSON.parse(jsonText); // Verify valid JSON

            const res = await fetch('/api/system/restore_config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: jsonText
            });

            if (res.ok) {
                alert('Khôi phục cấu hình thành công! Gateway đang khởi động lại...');
                startRebootCountdown();
            } else {
                alert('Lỗi khôi phục cấu hình!');
            }
        } catch (err) {
            alert('File JSON không hợp lệ: ' + err.message);
        }
    };

    reader.readAsText(file);
}

// 13. REBOOT MODAL & AUTO RECONNECT
async function rebootGateway() {
    if (!confirm('Bạn có chắc chắn muốn khởi động lại HC001 Gateway?')) return;
    try {
        await fetch('/api/reboot', { method: 'POST' });
        startRebootCountdown();
    } catch (err) {
        startRebootCountdown();
    }
}

function startRebootCountdown() {
    const modal = document.getElementById('modal-reboot');
    const cdEl = document.getElementById('reboot-countdown');
    modal.style.display = 'flex';
    let timeLeft = 10;
    cdEl.innerText = timeLeft;

    const timer = setInterval(async () => {
        timeLeft--;
        cdEl.innerText = timeLeft;

        if (timeLeft <= 0) {
            clearInterval(timer);
            cdEl.innerText = 'Đang thử kết nối...';
            // Polling check ping
            let pingAttempts = 0;
            const pingInterval = setInterval(async () => {
                pingAttempts++;
                try {
                    const res = await fetch('/api/status');
                    if (res.ok) {
                        clearInterval(pingInterval);
                        modal.style.display = 'none';
                        location.reload();
                    }
                } catch (e) {}

                if (pingAttempts >= 10) {
                    clearInterval(pingInterval);
                    modal.style.display = 'none';
                    location.reload();
                }
            }, 1500);
        }
    }, 1000);
}

async function resetDefaults() {
    if (!confirm('CẢNH BÁO: Thao tác này sẽ xóa toàn bộ cấu hình và đưa Gateway về trạng thái xuất xưởng. Bạn có chắc chắn muốn tiếp tục?')) return;
    try {
        await fetch('/api/reset_defaults', { method: 'POST' });
        alert('Gateway đang khôi phục cài đặt gốc... Vui lòng kết nối vào Wi-Fi Hotspot mặc định sau 15 giây.');
        sessionStorage.removeItem('hc001_auth');
        startRebootCountdown();
    } catch (err) {
        startRebootCountdown();
    }
}

// 14. CHANGE PASSWORD & HELPERS
async function handleChangePassword(e) {
    e.preventDefault();
    const oldP = document.getElementById('pwd-old').value;
    const newP = document.getElementById('pwd-new').value;
    const cfmP = document.getElementById('pwd-confirm').value;

    if (newP !== cfmP) {
        showToast('Mật khẩu xác nhận không khớp!', 'error');
        return;
    }

    try {
        const res = await fetch('/api/auth/change_password', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ old_password: oldP, new_password: newP })
        });
        const data = await res.json();
        if (res.ok && data.success) {
            showToast('Đổi mật khẩu thành công!', 'success');
            document.getElementById('form-change-password').reset();
        } else {
            showToast(data.error || 'Lỗi đổi mật khẩu!', 'error');
        }
    } catch (err) {
        showToast('Lỗi mạng: ' + err.message, 'error');
    }
}

async function injectTestPacket() {
    try {
        const res = await fetch('/api/test/inject_packet', { method: 'POST' });
        if (res.ok) {
            showToast('Đã bơm gói tin Heartbeat mẫu (single-log.json) lên EMQX & ChirpStack!', 'success');
            fetchStatus();
            loadSensors();
        }
    } catch (err) {
        showToast('Lỗi gửi gói tin: ' + err.message, 'error');
    }
}

function copyEUI() {
    if (!g_gatewayEUI || g_gatewayEUI === '--') return;
    navigator.clipboard.writeText(g_gatewayEUI).then(() => {
        showToast(`Đã sao chép EUI: ${g_gatewayEUI}`, 'success');
    }).catch(() => {
        showToast('Không thể sao chép tự động', 'error');
    });
}

function togglePassVisibility(inputId) {
    const input = document.getElementById(inputId);
    if (!input) return;
    input.type = (input.type === 'password') ? 'text' : 'password';
}

function showToast(message, type = 'success') {
    const container = document.getElementById('toast-container');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `toast ${type === 'success' ? 'toast-success' : 'toast-error'}`;
    toast.innerHTML = `<span>${type === 'success' ? '✓' : '⚠️'}</span> <span>${message}</span>`;
    container.appendChild(toast);

    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateX(40px)';
        toast.style.transition = 'all 0.3s ease';
        setTimeout(() => toast.remove(), 300);
    }, 3200);
}

function initApp() {
    loadConfig();
    loadSensors();
    fetchStatus();
    if (!g_statusInterval) {
        g_statusInterval = setInterval(fetchStatus, 3000);
    }
}

window.addEventListener('DOMContentLoaded', () => {
    checkAuth();
});


// WIREGUARD VPN CONFIG SAVE
async function saveWireGuardConfig() {
    const payload = {
        wgEnable: document.getElementById('cfg-wg-enable').checked,
        wgLocalIP: document.getElementById('cfg-wg-local-ip').value.trim(),
        wgSubnetMask: document.getElementById('cfg-wg-subnet').value.trim(),
        wgGateway: document.getElementById('cfg-wg-gateway').value.trim(),
        wgEndpointAddress: document.getElementById('cfg-wg-endpoint').value.trim(),
        wgEndpointPort: parseInt(document.getElementById('cfg-wg-port').value) || 51820,
        wgPrivateKey: document.getElementById('cfg-wg-private-key').value.trim(),
        wgPeerPublicKey: document.getElementById('cfg-wg-peer-public-key').value.trim()
    };

    try {
        const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        if (res.ok) {
            showToast('Đã lưu cấu hình WireGuard VPN thành công!', 'success');
            fetchStatus();
        } else {
            showToast('Lỗi: ' + (data.error || 'Không thể lưu cấu hình'), 'danger');
        }
    } catch (err) {
        showToast('Lỗi kết nối: ' + err.message, 'danger');
    }
}
