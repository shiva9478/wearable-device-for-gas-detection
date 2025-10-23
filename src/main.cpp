#include <esp_now.h>
#include <WiFi.h>
#include <DHT.h>
#include <SPIFFS.h>

uint8_t receiverMAC[] = {0xB8, 0xD6, 0x1A, 0xA7, 0x66, 0x88};

#define DHT_PIN 4
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// MQ-135 Air Quality Sensor Configuration
// Detects: CO, ammonia, benzene, alcohol, smoke
#define MQ135_PIN 35  // Analog input pin for MQ-135 sensor
#define MQ_VOLTAGE_RESOLUTION 3.3  // ESP32 ADC voltage
#define MQ_ADC_RESOLUTION 4095  // 12-bit ADC (0-4095)

// MQ-2 LPG Sensor Configuration
// Detects: LPG, propane, methane, hydrogen, alcohol, smoke
#define LPG_SENSOR_PIN 34  // Analog input pin for LPG sensor
#define LPG_VOLTAGE_RESOLUTION 3.3  // ESP32 ADC voltage
#define LPG_ADC_RESOLUTION 4095  // 12-bit ADC (0-4095)

typedef struct {
  float temperature;
  float humidity;
  int mq135Value;      // Air quality sensor value
  int lpgValue;        // LPG sensor value
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
int readMQ135Sensor();
int readLPGSensor();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Sender Started");
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("DHT22 initialized");
  
  // Initialize MQ-135 Air Quality sensor pin
  pinMode(MQ135_PIN, INPUT);
  Serial.println("MQ-135 Air Quality Sensor initialized on pin 35");
  Serial.println("  Detects: CO, ammonia, benzene, alcohol, smoke");
  
  // Initialize LPG sensor pin
  pinMode(LPG_SENSOR_PIN, INPUT);
  Serial.println("LPG Sensor initialized on pin 34");
  Serial.println("  Detects: LPG, propane, methane, hydrogen");
  
  // Configure ADC for both sensors
  analogReadResolution(12);  // Set ADC resolution to 12-bit
  analogSetAttenuation(ADC_11db);  // Set attenuation for full 0-3.3V range
  Serial.println("ADC configured: 12-bit resolution, 0-3.3V range");
  
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

int readMQ135Sensor() {
  // Read analog value from MQ-135 Air Quality sensor
  int rawValue = analogRead(MQ135_PIN);
  
  // Optional: Calculate voltage
  // float voltage = (rawValue / (float)MQ_ADC_RESOLUTION) * MQ_VOLTAGE_RESOLUTION;
  
  return rawValue;
}

int readLPGSensor() {
  // Read analog value from LPG sensor
  int rawValue = analogRead(LPG_SENSOR_PIN);
  
  // Optional: Calculate voltage
  // float voltage = (rawValue / (float)LPG_ADC_RESOLUTION) * LPG_VOLTAGE_RESOLUTION;
  
  return rawValue;
}

void readSensors() {
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temp) || isnan(humidity)) {
    return;
  }
  
  sensorData.temperature = temp;
  sensorData.humidity = humidity;
  sensorData.mq135Value = readMQ135Sensor();  // Read MQ-135 air quality sensor
  sensorData.lpgValue = readLPGSensor();      // Read LPG sensor
  sensorData.messageNumber = ++messageNumber;
}

void sendData() {
  sentCount++;
  
  Serial.printf("T:%.1fC H:%.1f%% AirQ:%d LPG:%d\n", 
                sensorData.temperature,
                sensorData.humidity,
                sensorData.mq135Value,
                sensorData.lpgValue);
  
  esp_now_send(receiverMAC, (uint8_t *)&sensorData, sizeof(sensorData));
}

void storeFailedData(SensorData data) {
  File file = SPIFFS.open(STORAGE_FILE, "a");
  if (!file) {
    Serial.println("Failed to open storage file");
    return;
  }
  
  char buffer[120];
  snprintf(buffer, sizeof(buffer), "%.1f,%.1f,%d,%d,%d\n",
           data.temperature, data.humidity, data.mq135Value, data.lpgValue, data.messageNumber);
  
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
        int comma4 = line.indexOf(',', comma3 + 1);
        
        float temp = line.substring(0, comma1).toFloat();
        float humidity = line.substring(comma1 + 1, comma2).toFloat();
        int mq135 = line.substring(comma2 + 1, comma3).toInt();
        int lpg = line.substring(comma3 + 1, comma4).toInt();
        
        SensorData storedData;
        storedData.temperature = temp;
        storedData.humidity = humidity;
        storedData.mq135Value = mq135;
        storedData.lpgValue = lpg;
        storedData.messageNumber = line.substring(comma4 + 1).toInt();
        
        esp_now_send(receiverMAC, (uint8_t *)&storedData, sizeof(storedData));
        Serial.printf("Sent stored: T:%.1f H:%.1f AirQ:%d LPG:%d\n", temp, humidity, mq135, lpg);
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
  Serial.printf("Last - T:%.1fC H:%.1f%% AirQ:%d LPG:%d\n",
                sensorData.temperature,
                sensorData.humidity,
                sensorData.mq135Value,
                sensorData.lpgValue);
  
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
