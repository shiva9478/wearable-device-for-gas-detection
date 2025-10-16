#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FirebaseESP32.h>
#include <esp_task_wdt.h>

// ==================== FIREBASE CREDENTIALS ====================
#define FIREBASE_HOST "elites1-a4e94-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "AIzaSyBAV_aiAL2txd2jQ1zIBVVXwg0DwSEvCfo"

// ==================== WIFI CREDENTIALS ====================
#define WIFI_SSID "Andromeda"
#define WIFI_PASSWORD "09876543"

// ==================== WATCHDOG TIMEOUT ====================
#define WDT_TIMEOUT 30

// ==================== GLOBAL VARIABLES ====================
FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

uint32_t messageCount = 0;
uint32_t uploadCount = 0;
uint32_t uploadFailed = 0;
bool senderConnected = false;
unsigned long lastStatusUpdate = 0;

// ==================== SENSOR DATA STRUCTURE ====================
typedef struct {
  float temperature;
  float humidity;
  int mqValue;
  int messageNumber;
  unsigned long timestamp;
} SensorData;

SensorData latestData;
bool hasNewData = false;

// ==================== FUNCTION DECLARATIONS ====================
void printStartupBanner();
void initWiFi();
void initESPNOW();
void initFirebase();
void onDataReceived(const uint8_t *mac, const uint8_t *data, int len);
void uploadLatestData();
void updateRealtimeData();
void printStatus();
bool parseTextData(const char* text, SensorData &data);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  
  printStartupBanner();
  
  initWiFi();
  initESPNOW();
  initFirebase();
  
  Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("🚀 Setup complete! System ready.");
  Serial.printf("📡 ESP-NOW listening on channel %d\n", WiFi.channel());
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ==================== LOOP ====================
void loop() {
  esp_task_wdt_reset();
  
  // Upload new data immediately (non-blocking)
  if (hasNewData) {
    hasNewData = false;
    updateRealtimeData();  // Update current readings
    // Skip historical data upload to reduce load
  }
  
  if (millis() - lastStatusUpdate > 10000) {
    printStatus();
    lastStatusUpdate = millis();
  }
  
  delay(50);
}

// ==================== STARTUP BANNER ====================
void printStartupBanner() {
  Serial.println("\n\n");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("   🏥 ESP32 HEALTH MONITOR - RECEIVER   ");
  Serial.println("   📊 Real-time Mode - Optimized        ");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}

// ==================== WIFI INITIALIZATION ====================
void initWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("🔍 Scanning for network...");
  int n = WiFi.scanNetworks();
  int targetChannel = 1;
  bool found = false;
  
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      targetChannel = WiFi.channel(i);
      found = true;
      Serial.printf("✅ Found network on channel: %d\n", targetChannel);
      break;
    }
  }
  
  if (!found) {
    Serial.println("⚠️ Network not found, using default channel 1");
  }
  
  Serial.println("📶 Connecting...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    esp_task_wdt_reset();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("   Channel: %d\n", WiFi.channel());
    
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    
    IPAddress dns1(8, 8, 8, 8);
    IPAddress dns2(8, 8, 4, 4);
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
    Serial.println("✅ DNS configured");
    
    delay(1000);
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    ESP.restart();
  }
}

// ==================== ESP-NOW INITIALIZATION ====================
void initESPNOW() {
  int channel = WiFi.channel();
  Serial.printf("Configuring ESP-NOW on channel %d\n", channel);
  
  esp_now_deinit();
  delay(100);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed!");
    ESP.restart();
    return;
  }
  
  Serial.println("✅ ESP-NOW initialized");
  
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("📟 Receiver MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
  esp_now_register_recv_cb(onDataReceived);
  Serial.println("✅ ESP-NOW callback registered");
}

// ==================== FIREBASE INITIALIZATION ====================
void initFirebase() {
  firebaseConfig.host = FIREBASE_HOST;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  // Very short timeout for real-time updates
  firebaseConfig.timeout.serverResponse = 3 * 1000;
  firebaseConfig.timeout.socketConnection = 3 * 1000;
  
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);
  
  firebaseData.setBSSLBufferSize(512, 512);
  
  Serial.println("✅ Firebase initialized");
  
  // Initialize database structure
  esp_task_wdt_reset();
  Firebase.setString(firebaseData, "/current/status", "online");
  Firebase.setFloat(firebaseData, "/current/temperature", 0.0);
  Firebase.setFloat(firebaseData, "/current/humidity", 0.0);
  Firebase.setInt(firebaseData, "/current/mqValue", 0);
  Serial.println("✅ Database structure initialized");
}

// ==================== ESP-NOW DATA RECEIVED CALLBACK ====================
void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  messageCount++;
  senderConnected = true;
  
  char receivedText[256];
  memcpy(receivedText, data, len);
  receivedText[len] = '\0';
  
  SensorData tempData;
  if (parseTextData(receivedText, tempData)) {
    latestData = tempData;
    latestData.timestamp = millis();
    hasNewData = true;
    
    Serial.printf("\r📡 Msg#%d | 🌡️%.1f°C | 💧%.1f%% | 🔬%d        ", 
                  messageCount, latestData.temperature, 
                  latestData.humidity, latestData.mqValue);
  }
}

// ==================== PARSE TEXT DATA ====================
bool parseTextData(const char* text, SensorData &data) {
  data.temperature = 0;
  data.humidity = 0;
  data.mqValue = 0;
  data.messageNumber = 0;
  
  String dataStr = String(text);
  bool isStoredMessage = dataStr.startsWith("STORED|");
  
  if (isStoredMessage) {
    dataStr = dataStr.substring(7);
  }
  
  int tIndex = dataStr.indexOf("T:");
  int hIndex = dataStr.indexOf("H:");
  int mqIndex = dataStr.indexOf("MQ:");
  
  if (tIndex != -1 && hIndex != -1 && mqIndex != -1) {
    int tEnd = dataStr.indexOf("|", tIndex);
    if (tEnd != -1) {
      data.temperature = dataStr.substring(tIndex + 2, tEnd).toFloat();
    }
    
    int hEnd = dataStr.indexOf("|", hIndex);
    if (hEnd != -1) {
      data.humidity = dataStr.substring(hIndex + 2, hEnd).toFloat();
    }
    
    int mqEnd = dataStr.indexOf("|", mqIndex);
    if (mqEnd == -1) mqEnd = dataStr.length();
    data.mqValue = dataStr.substring(mqIndex + 3, mqEnd).toInt();
    
    if (isStoredMessage) {
      int timeIndex = dataStr.indexOf("Time:");
      if (timeIndex != -1) {
        data.messageNumber = dataStr.substring(timeIndex + 5).toInt();
      }
    } else {
      int msgIndex = dataStr.indexOf("|#");
      if (msgIndex != -1) {
        data.messageNumber = dataStr.substring(msgIndex + 2).toInt();
      }
    }
    
    return true;
  }
  
  return false;
}

// ==================== UPDATE REALTIME DATA ====================
void updateRealtimeData() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  esp_task_wdt_reset();
  
  // Use Firebase stream for real-time updates (faster than setJSON)
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 200) return;  // Max 5 updates/sec
  
  lastUpdate = millis();
  
  // Update current values only (much faster than full JSON)
  bool success = true;
  success &= Firebase.setFloat(firebaseData, "/current/temperature", latestData.temperature);
  success &= Firebase.setFloat(firebaseData, "/current/humidity", latestData.humidity);
  success &= Firebase.setInt(firebaseData, "/current/mqValue", latestData.mqValue);
  success &= Firebase.setInt(firebaseData, "/current/messageNumber", latestData.messageNumber);
  success &= Firebase.setInt(firebaseData, "/current/timestamp", latestData.timestamp);
  
  if (success) {
    uploadCount++;
  } else {
    uploadFailed++;
  }
  
  esp_task_wdt_reset();
}

// ==================== PRINT STATUS ====================
void printStatus() {
  Serial.println("\n\n━━━━━━━━━━━━━━ STATUS ━━━━━━━━━━━━━");
  Serial.printf("📊 Received: %d | Uploaded: %d | Failed: %d\n", 
                messageCount, uploadCount, uploadFailed);
  Serial.printf("📶 WiFi: %s (Ch %d)\n", 
                WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected",
                WiFi.channel());
  Serial.printf("🔗 Sender: %s\n", senderConnected ? "Active" : "Waiting");
  Serial.printf("🔥 Firebase: %s\n", Firebase.ready() ? "Ready" : "Not Ready");
  Serial.printf("📈 Success Rate: %.1f%%\n", 
                messageCount > 0 ? (float)uploadCount/messageCount*100 : 0);
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}