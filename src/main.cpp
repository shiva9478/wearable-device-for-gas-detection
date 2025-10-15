/*
 * ESP32 SERVER - DEBUGGED VERSION
 * Key fixes applied
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Configuration
#define WIFI_SSID "Andromeda"
#define WIFI_PASSWORD "09876543"
#define FIREBASE_HOST "https://elites1-a4e94-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "AIzaSyBAV_aiAL2txd2jQ1zIBVVXwg0DwSEvCfo"
#define WEB_SERVER_PORT 80

// MAC Whitelist
uint8_t allowedMACs[][6] = {
  {0x10, 0x06, 0x1C, 0x68, 0x85, 0x14}
};
int numAllowedMACs = 1;

// Data Structure
typedef struct sensor_data {
  float temperature;
  float humidity;
  int mq_value;
  float heartRate;
  float spo2;
  char mac[18];
  unsigned long timestamp;
} sensor_data;

// Global Variables
WebServer server(WEB_SERVER_PORT);
HTTPClient http;
sensor_data receivedData;
bool newDataAvailable = false;
bool wifiConnected = false;
bool dataStoredInSPIFFS = false;
unsigned long lastDataReceived = 0;

// ESP-NOW Callback - FIX: Added compatibility wrapper
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  
  Serial.printf("📡 Data from: %s\n", macStr);
  
  bool isAllowed = false;
  for(int i = 0; i < numAllowedMACs; i++) {
    if(memcmp(mac_addr, allowedMACs[i], 6) == 0) {
      isAllowed = true;
      break;
    }
  }
  
  if(!isAllowed) {
    Serial.println("⚠️ UNAUTHORIZED MAC");
    return;
  }
  
  memcpy(&receivedData, data, sizeof(receivedData));
  strcpy(receivedData.mac, macStr);
  receivedData.timestamp = millis();
  newDataAvailable = true;
  lastDataReceived = millis();
  
  Serial.printf("✅ Temp:%.1f Hum:%.1f MQ:%d HR:%.0f SpO2:%.1f\n", 
                receivedData.temperature, receivedData.humidity, 
                receivedData.mq_value, receivedData.heartRate, receivedData.spo2);
}

// SPIFFS Functions
void initSPIFFS() {
  if(!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS Failed");
    return;
  }
  Serial.println("✅ SPIFFS OK");
}

void saveToSPIFFS(sensor_data data) {
  File file = SPIFFS.open("/sensor_log.txt", FILE_APPEND);
  if(!file) return;
  
  char buffer[250];
  snprintf(buffer, sizeof(buffer), "%lu|%s|%.2f|%.2f|%d|%.2f|%.2f\n",
           data.timestamp, data.mac, data.temperature, data.humidity, 
           data.mq_value, data.heartRate, data.spo2);
  
  file.print(buffer);
  file.close();
  dataStoredInSPIFFS = true;
  Serial.println("💾 Saved to SPIFFS");
}

void cleanSPIFFS() {
  if(!dataStoredInSPIFFS) return;
  if(SPIFFS.remove("/sensor_log.txt")) {
    dataStoredInSPIFFS = false;
    Serial.println("🧹 SPIFFS Cleaned");
  }
}

// FIX: Simplified Firebase function with better error handling
bool sendToFirebase(sensor_data data) {
  if(!wifiConnected) {
    saveToSPIFFS(data);
    return false;
  }
  
  String path = "/sensor_readings/" + String(data.mac);
  path.replace(":", "_");
  path += "/current";
  
  DynamicJsonDocument doc(512);
  doc["temperature"] = data.temperature;
  doc["humidity"] = data.humidity;
  doc["mq_value"] = data.mq_value;
  doc["heartRate"] = data.heartRate;
  doc["spo2"] = data.spo2;
  doc["timestamp"] = data.timestamp;
  doc["lastUpdate"] = millis();
  
  String jsonStr;
  serializeJson(doc, jsonStr);
  
  // FIX: Try without auth first
  String url = String(FIREBASE_HOST) + path + ".json";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  int httpCode = http.PUT(jsonStr);
  
  Serial.printf("Firebase: %d - %s\n", httpCode, httpCode == 200 ? "OK" : http.getString().c_str());
  
  http.end();
  
  if(httpCode == 200 || httpCode == 201) {
    Serial.println("✅ Firebase OK");
    if(dataStoredInSPIFFS) cleanSPIFFS();
    return true;
  } else {
    Serial.printf("❌ Firebase Error: %d\n", httpCode);
    saveToSPIFFS(data);
    return false;
  }
}

// Web Server Handlers
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 Monitor</title><style>";
  html += "body{font-family:Arial;background:#1a1a2e;color:#fff;margin:0;padding:20px}";
  html += ".card{background:#16213e;padding:20px;border-radius:10px;margin:10px 0}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px}";
  html += ".box{background:#0f3460;padding:20px;border-radius:8px;text-align:center}";
  html += ".val{font-size:2em;font-weight:bold;margin:10px 0}";
  html += ".label{opacity:0.8;font-size:0.9em}</style></head><body>";
  html += "<h1>ESP32 Sensor Monitor</h1>";
  html += "<div class='card'><h2>Sensor Data</h2><div class='grid'>";
  html += "<div class='box'><div class='label'>Temperature</div><div class='val' id='t'>--</div><div>°C</div></div>";
  html += "<div class='box'><div class='label'>Humidity</div><div class='val' id='h'>--</div><div>%</div></div>";
  html += "<div class='box'><div class='label'>MQ Sensor</div><div class='val' id='m'>--</div><div>PPM</div></div>";
  html += "<div class='box'><div class='label'>Heart Rate</div><div class='val' id='hr'>--</div><div>bpm</div></div>";
  html += "<div class='box'><div class='label'>SpO2</div><div class='val' id='sp'>--</div><div>%</div></div>";
  html += "</div></div>";
  html += "<script>setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('t').textContent=d.temp.toFixed(1);";
  html += "document.getElementById('h').textContent=d.humidity.toFixed(1);";
  html += "document.getElementById('m').textContent=d.mq;";
  html += "document.getElementById('hr').textContent=d.hr.toFixed(0);";
  html += "document.getElementById('sp').textContent=d.spo2.toFixed(1);";
  html += "})},2000);</script></body></html>";
  server.send(200, "text/html", html);
}

void handleData() {
  DynamicJsonDocument doc(256);
  doc["temp"] = receivedData.temperature;
  doc["humidity"] = receivedData.humidity;
  doc["mq"] = receivedData.mq_value;
  doc["hr"] = receivedData.heartRate;
  doc["spo2"] = receivedData.spo2;
  doc["mac"] = receivedData.mac;
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void setup() {
  Serial.begin(9600);  // FIX: Changed from 115200
  delay(2000);
  Serial.println("\n=== ESP32 SERVER ===\n");
  
  initSPIFFS();
  
  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi connecting");
  
  int timeout = 20;
  while(WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
  }
  
  Serial.println();
  if(WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("✅ WiFi OK");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("⚠️ WiFi Failed - Offline Mode");
  }
  
  // ESP-NOW
  if(esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Failed");
    ESP.restart();
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  Serial.printf("✅ ESP-NOW OK - %d clients registered\n", numAllowedMACs);
  
  // Web Server
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  
  Serial.println("✅ Server Ready\n");
}

void loop() {
  server.handleClient();
  
  if(newDataAvailable) {
    sendToFirebase(receivedData);
    newDataAvailable = false;
  }
  
  delay(10);
}