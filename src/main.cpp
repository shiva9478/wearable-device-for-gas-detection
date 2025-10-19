#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <SPIFFS.h>

uint8_t receiverMAC[] = {0xB8, 0xD6, 0x1A, 0xA7, 0x66, 0x88};

#define DHT_PIN 4
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

#define GAS_PIN 35

typedef struct {
  float temperature;
  float humidity;
  int gasValue;
  uint32_t messageNumber;
} SensorData;

SensorData sensorData;
uint32_t messageNumber = 0;
uint32_t sentCount = 0;
uint32_t successCount = 0;
unsigned long lastStatusTime = 0;
const char* STORAGE_FILE = "/spiffs/data.txt";

void initESPNOW();
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void readSensors();
void sendData();
void printStatus();
void initSPIFFS();
void storeFailedData(SensorData data);
void sendStoredData();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Sender Started");
  
  dht.begin();
  Serial.println("DHT22 initialized");
  
  initSPIFFS();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  initESPNOW();
  
  Serial.println("Ready to send data");
}

void loop() {
  readSensors();
  sendData();
  sendStoredData();
  
  if (millis() - lastStatusTime > 10000) {
    printStatus();
    lastStatusTime = millis();
  }
  
  delay(2000);
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS init failed");
    return;
  }
  Serial.println("SPIFFS initialized");
}

void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  
  Serial.println("ESP-NOW initialized");
  
  esp_now_register_send_cb(onDataSent);
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("Receiver peer added");
  } else {
    Serial.println("Failed to add receiver");
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    successCount++;
  }
}

void readSensors() {
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temp) || isnan(humidity)) {
    return;
  }
  
  sensorData.temperature = temp;
  sensorData.humidity = humidity;
  sensorData.gasValue = analogRead(GAS_PIN);
  sensorData.messageNumber = ++messageNumber;
}

void sendData() {
  sentCount++;
  
  Serial.printf("T:%.1fC H:%.1f%% G:%d\n", 
                sensorData.temperature,
                sensorData.humidity,
                sensorData.gasValue);
  
  esp_now_send(receiverMAC, (uint8_t *)&sensorData, sizeof(sensorData));
}

void storeFailedData(SensorData data) {
  File file = SPIFFS.open(STORAGE_FILE, "a");
  if (!file) {
    Serial.println("Failed to open storage file");
    return;
  }
  
  char buffer[100];
  snprintf(buffer, sizeof(buffer), "%.1f,%.1f,%d,%d\n",
           data.temperature, data.humidity, data.gasValue, data.messageNumber);
  
  file.print(buffer);
  file.close();
  Serial.println("Data stored to SPIFFS");
}

void sendStoredData() {
  if (!SPIFFS.exists(STORAGE_FILE)) {
    return;
  }
  
  File file = SPIFFS.open(STORAGE_FILE, "r");
  if (!file) {
    return;
  }
  
  String line = "";
  while (file.available()) {
    char c = file.read();
    if (c == '\n') {
      if (line.length() > 0) {
        int comma1 = line.indexOf(',');
        int comma2 = line.indexOf(',', comma1 + 1);
        int comma3 = line.indexOf(',', comma2 + 1);
        
        float temp = line.substring(0, comma1).toFloat();
        float humidity = line.substring(comma1 + 1, comma2).toFloat();
        int gas = line.substring(comma2 + 1, comma3).toInt();
        
        SensorData storedData;
        storedData.temperature = temp;
        storedData.humidity = humidity;
        storedData.gasValue = gas;
        storedData.messageNumber = line.substring(comma3 + 1).toInt();
        
        esp_now_send(receiverMAC, (uint8_t *)&storedData, sizeof(storedData));
        Serial.printf("Sent stored: T:%.1f H:%.1f G:%d\n", temp, humidity, gas);
        delay(100);
      }
      line = "";
    } else {
      line += c;
    }
  }
  
  file.close();
  SPIFFS.remove(STORAGE_FILE);
  Serial.println("Stored data cleared");
}

void printStatus() {
  Serial.println("\n━━━━━━━━━━━━━━━━ STATUS ━━━━━━━━━━━━━━━━");
  Serial.printf("Sent: %d | Success: %d | Failed: %d\n", 
                sentCount, successCount, sentCount - successCount);
  Serial.printf("Success Rate: %.1f%%\n", 
                sentCount > 0 ? (float)successCount / sentCount * 100 : 0);
  Serial.printf("Last - T:%.1fC H:%.1f%% G:%d\n",
                sensorData.temperature,
                sensorData.humidity,
                sensorData.gasValue);
  
  if (SPIFFS.exists(STORAGE_FILE)) {
    File file = SPIFFS.open(STORAGE_FILE, "r");
    int lines = 0;
    while (file.available()) {
      if (file.read() == '\n') lines++;
    }
    file.close();
    Serial.printf("Stored messages: %d\n", lines);
  }
  
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}