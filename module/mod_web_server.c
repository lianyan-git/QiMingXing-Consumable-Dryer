#ifndef BOOTLOADER_BUILD
/*
 * mod_web_server.c
 * Web Server for WiFi setup and OTA
 */
#include "mod_web_server.h"
#include "system_config.h"
#include "mod_wifi_manager.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>

#define MAX_SSID_LIST   20
#define SSID_LEN        32

static char scan_ssids[MAX_SSID_LIST][SSID_LEN];
static uint8_t scan_count = 0;

static void SendPage(int conn_id, const char *page, uint16_t len);
static void SendSensorData(int conn_id);
static void SendScanResult(int conn_id);
static void HandleWiFiConnect(int conn_id, uint8_t *data, uint16_t len);
static void HandleFirmwareUpload(int conn_id, uint8_t *data, uint16_t len);
static void HandleUpdate(int conn_id);
static void Send404(int conn_id);

static const char *page_main =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
    "<title>Dryer Controller</title>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box;}"
    "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Arial,sans-serif;"
    "background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);min-height:100vh;color:#fff;padding:20px;}"
    ".header{text-align:center;padding:20px 0;border-bottom:2px solid #e94560;margin-bottom:20px;}"
    ".header h1{font-size:28px;color:#e94560;}"
    ".card{background:rgba(255,255,255,0.05);border-radius:16px;padding:20px;margin-bottom:16px;"
    "backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1);}"
    ".card-title{font-size:14px;color:#888;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;}"
    ".card-value{font-size:36px;font-weight:700;color:#fff;}"
    ".temp{color:#ff6b6b;}.hum{color:#4ecdc4;}.time{color:#ffe66d;}.weight{color:#a8e6cf;}"
    ".btn{width:100%;padding:16px;border:none;border-radius:12px;font-size:16px;font-weight:600;"
    "cursor:pointer;transition:all 0.3s;margin-top:10px;}"
    ".btn-primary{background:#e94560;color:#fff;}.btn-primary:hover{background:#c13651;transform:translateY(-2px);}"
    ".btn-secondary{background:#0f3460;color:#fff;}.btn-secondary:hover{background:#1a4a7a;}"
    ".btn-success{background:#28a745;color:#fff;}.btn-success:hover{background:#218838;}"
    ".input{width:100%;padding:14px;border:2px solid rgba(255,255,255,0.1);border-radius:10px;"
    "background:rgba(255,255,255,0.05);color:#fff;font-size:16px;margin:8px 0;}"
    ".input:focus{outline:none;border-color:#e94560;}"
    ".ssid-list{max-height:300px;overflow-y:auto;margin:10px 0;}"
    ".ssid-item{padding:14px;margin:6px 0;background:rgba(255,255,255,0.05);border-radius:10px;"
    "cursor:pointer;transition:all 0.2s;display:flex;align-items:center;}"
    ".ssid-item:hover{background:rgba(233,69,96,0.2);border:1px solid #e94560;}"
    ".ssid-item.selected{background:rgba(233,69,96,0.3);border:2px solid #e94560;}"
    ".ssid-icon{font-size:20px;margin-right:12px;}.ssid-name{flex:1;font-size:16px;}.ssid-lock{font-size:14px;color:#888;}"
    ".progress-bar{height:8px;background:rgba(255,255,255,0.1);border-radius:4px;overflow:hidden;margin:10px 0;}"
    ".progress-fill{height:100%;background:#28a745;width:0%%;transition:width 0.5s;}"
    ".hidden{display:none !important;}"
    ".section-title{font-size:18px;font-weight:600;margin-bottom:12px;color:#e94560;}"
    ".status-badge{display:inline-block;padding:6px 14px;border-radius:20px;font-size:12px;background:#28a745;color:#fff;margin-top:10px;}"
    "</style></head><body>"
    "<div class=\"header\"><h1>Dryer Controller</h1><p style=\"color:#888;margin-top:8px\">Smart Drying System</p></div>"
    "<div class=\"card\"><div class=\"card-title\">Temperature</div><div class=\"card-value temp\" id=\"temp\">--C</div></div>"
    "<div class=\"card\"><div class=\"card-title\">Humidity</div><div class=\"card-value hum\" id=\"hum\">--%</div></div>"
    "<div class=\"card\"><div class=\"card-title\">Remaining Time</div><div class=\"card-value time\" id=\"time\">--:--</div></div>"
    "<div class=\"card\"><div class=\"card-title\">Weight</div><div class=\"card-value weight\" id=\"weight\">--g</div></div>"
    "<div class=\"card\" id=\"wifiCard\"><div class=\"section-title\">WiFi Setup</div>"
    "<div id=\"wifiStatus\"><p style=\"color:#888;margin-bottom:10px\">Scan nearby WiFi networks</p>"
    "<button class=\"btn btn-primary\" onclick=\"scanWiFi()\">Scan WiFi</button></div>"
    "<div id=\"scanResult\" class=\"hidden\"><p>Select a network:</p><div class=\"ssid-list\" id=\"ssidList\"></div>"
    "<div id=\"passwordArea\" class=\"hidden\"><input type=\"password\" class=\"input\" id=\"wifiPass\" placeholder=\"Enter WiFi password\">"
    "<button class=\"btn btn-success\" onclick=\"connectWiFi()\">Connect</button></div>"
    "<button class=\"btn btn-secondary\" onclick=\"backToScan()\" style=\"margin-top:10px\">Back</button></div>"
    "<div id=\"connectingStatus\" class=\"hidden\"><p>Connecting...</p><div class=\"progress-bar\"><div class=\"progress-fill\" style=\"width:60%%\"></div></div></div></div>"
    "<div class=\"card\"><div class=\"section-title\">Firmware Update</div>"
    "<input type=\"file\" class=\"input\" id=\"firmware\" accept=\".bin\">"
    "<div class=\"progress-bar\"><div class=\"progress-fill\" id=\"fwProgress\"></div></div>"
    "<button class=\"btn btn-primary\" onclick=\"uploadFirmware()\">Upload Firmware</button>"
    "<button class=\"btn btn-success hidden\" id=\"updateBtn\" onclick=\"doUpdate()\">Update Device</button></div>"
    "<script>"
    "setInterval(()=>{fetch('/api/data').then(r=>r.json()).then(d=>{"
    "document.getElementById('temp').textContent=d.temp+'C';"
    "document.getElementById('hum').textContent=d.hum+'%';"
    "document.getElementById('time').textContent=d.time;"
    "document.getElementById('weight').textContent=d.weight+'g';});},2000);"
    "let selectedSSID='';"
    "function scanWiFi(){document.getElementById('wifiStatus').classList.add('hidden');document.getElementById('scanResult').classList.remove('hidden');"
    "document.getElementById('ssidList').innerHTML='<p style=\"color:#888\">Scanning...</p>';"
    "fetch('/api/scan').then(r=>r.json()).then(data=>{let html='';data.networks.forEach((net,i)=>{"
    "html+='<div class=\"ssid-item\" onclick=\"selectSSID(this,\\'\"+net.name+\"')\">"
    "<span class=\"ssid-icon\">WiFi</span><span class=\"ssid-name\">'+net.name+'</span>"
    "<span class=\"ssid-lock\">'+(net.secure?'LOCK':'')+'</span></div>';});"
    "document.getElementById('ssidList').innerHTML=html;});}"
    "function selectSSID(el,ssid){document.querySelectorAll('.ssid-item').forEach(e=>e.classList.remove('selected'));"
    "el.classList.add('selected');selectedSSID=ssid;document.getElementById('passwordArea').classList.remove('hidden');}"
    "function backToScan(){document.getElementById('scanResult').classList.add('hidden');document.getElementById('wifiStatus').classList.remove('hidden');"
    "document.getElementById('passwordArea').classList.add('hidden');selectedSSID='';}"
    "function connectWiFi(){const pass=document.getElementById('wifiPass').value;"
    "if(!selectedSSID||!pass){alert('Please select WiFi and enter password');return;}"
    "document.getElementById('scanResult').classList.add('hidden');document.getElementById('connectingStatus').classList.remove('hidden');"
    "fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:selectedSSID,password:pass})})"
    ".then(r=>r.text()).then(msg=>{alert(msg);document.getElementById('connectingStatus').classList.add('hidden');document.getElementById('wifiStatus').classList.remove('hidden');});}"
    "let firmwareUploaded=false;"
    "function uploadFirmware(){const f=document.getElementById('firmware').files[0];if(!f)return;"
    "const xhr=new XMLHttpRequest();xhr.upload.onprogress=e=>{const p=(e.loaded/e.total*100).toFixed(0);"
    "document.getElementById('fwProgress').style.width=p+'%';};"
    "xhr.onload=()=>{firmwareUploaded=true;document.getElementById('updateBtn').classList.remove('hidden');};"
    "xhr.open('POST','/upload');xhr.send(f);}"
    "function doUpdate(){if(!firmwareUploaded)return;fetch('/update',{method:'POST'}).then(()=>{alert('Device is updating...');});}"
    "</script></body></html>";

void WebServer_Init(void) { memset(scan_ssids, 0, sizeof(scan_ssids)); scan_count = 0; }

void WebServer_SetScanResults(const char *ssids, uint8_t count)
{
    scan_count = 0;
    if (count > MAX_SSID_LIST) count = MAX_SSID_LIST;
    const char *p = ssids;
    for (uint8_t i = 0; i < count && *p; i++) {
        strncpy(scan_ssids[i], p, SSID_LEN-1);
        p += strlen(p) + 1;
        scan_count++;
    }
}

void WebServer_HandleRequest(int conn_id, uint8_t *data, uint16_t len)
{
    (void)conn_id; (void)len;
    if (len < 4) return;
    char method[8] = {0}, path[32] = {0};
    sscanf((char*)data, "%s %s", method, path);
    if (strcmp(path, "/") == 0 || strcmp(path, "/index") == 0) SendPage(conn_id, page_main, strlen(page_main));
    else if (strcmp(path, "/api/data") == 0) SendSensorData(conn_id);
    else if (strcmp(path, "/api/scan") == 0) SendScanResult(conn_id);
    else if (strcmp(path, "/api/connect") == 0) HandleWiFiConnect(conn_id, data, len);
    else if (strcmp(path, "/upload") == 0) HandleFirmwareUpload(conn_id, data, len);
    else if (strcmp(path, "/update") == 0) HandleUpdate(conn_id);
    else Send404(conn_id);
}

static void SendPage(int conn_id, const char *page, uint16_t len) { (void)conn_id; (void)page; (void)len; }

static void SendSensorData(int conn_id)
{
    (void)conn_id;
    char json[256];
    uint32_t secs = g_sys.remaining_sec;
    uint16_t hours = (uint16_t)(secs / 3600);
    uint16_t mins = (uint16_t)((secs % 3600) / 60);
    uint16_t secs_rem = (uint16_t)(secs % 60);
    sprintf(json, "{\"temp\":%.1f,\"hum\":%.1f,\"time\":\"%02d:%02d:%02d\",\"weight\":%d}",
            g_sys.current_temp, g_sys.current_humidity, hours, mins, secs_rem, (int)g_sys.weight_g);
    (void)json;
}

static void SendScanResult(int conn_id)
{
    (void)conn_id;
    char networks[1024] = {0};
    char item[128];
    for (uint8_t i = 0; i < scan_count; i++) {
        if (i > 0) strcat(networks, ",");
        sprintf(item, "{\"name\":\"%s\",\"secure\":true}", scan_ssids[i]);
        strcat(networks, item);
    }
    (void)networks;
}

static void HandleWiFiConnect(int conn_id, uint8_t *data, uint16_t len)
{
    (void)conn_id; (void)data; (void)len;
    char *body = strstr((char*)data, "\r\n\r\n");
    if (body) {
        body += 4;
        char ssid[32] = {0}, password[64] = {0};
        sscanf(body, "{\"ssid\":\"%[^\"]\",\"password\":\"%[^\"]\"}", ssid, password);
        WiFiManager_Connect(ssid, password);
    }
}

static void HandleFirmwareUpload(int conn_id, uint8_t *data, uint16_t len) { (void)conn_id; (void)data; (void)len; }
static void HandleUpdate(int conn_id) { (void)conn_id; }
static void Send404(int conn_id) { (void)conn_id; }
void WebServer_Process(void) {}
#endif /* BOOTLOADER_BUILD */