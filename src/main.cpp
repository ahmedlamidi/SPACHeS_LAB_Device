#include <esp_now.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Arduino.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>

// #include <BLEDevice.h>
// #include <BLEScan.h>
// #include <BLEAdvertisedDevice.h>


//NTP library to get real time from server
#define NTP_OFFSET   0 * 60      // In seconds
#define NTP_INTERVAL 5 * 1000    // In miliseconds
#define NTP_ADDRESS  "pool.ntp.org"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

// Structure example to receive data
// Must match the sender structure


constexpr char THINGSBOARD_SERVER[] = "131.247.15.226";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr char TOKEN[] = "spo2_123";
constexpr uint16_t MAX_MESSAGE_SIZE = 128U;
unsigned long long start_epoch_time;
unsigned long long start_milli_time = 0;


// MQTT and ThingsBoard objects
WiFiClient espClient;
Arduino_MQTT_Client mqttClient(espClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);




#define QUEUE_SIZE 20

struct TelemetryData {
  int32_t n_spo2;  //SPO2 value
  int8_t ch_spo2_valid;  //indicator to show if the SPO2 calculation is valid
  int32_t n_heart_rate; //heart rate value
  int8_t  ch_hr_valid;  //indicator to show if the heart rate calculation is valid
  unsigned long start_milli_time;
  uint16_t PPG_R;
  uint16_t PPG_IR;
};

// Ring buffer
TelemetryData telemetryQueue[QUEUE_SIZE];
volatile int queueHead = 0;
volatile int queueTail = 0;

bool isQueueFull() {
  return ((queueHead + 1) % QUEUE_SIZE) == queueTail;
}

bool isQueueEmpty() {
  return queueHead == queueTail;
}

bool enqueue(const TelemetryData& data) {
  if (isQueueFull()) return false;
  telemetryQueue[queueHead] = data;
  queueHead = (queueHead + 1) % QUEUE_SIZE;
  return true;
}

bool dequeue(TelemetryData &data) {
  if (isQueueEmpty()) return false;
  data = telemetryQueue[queueTail];
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  return true;
}


// Create a struct_message called myData
TelemetryData myData;

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(TelemetryData));
  enqueue(myData); // Add to queue

  // Get timestamp
}

// Web Server and AutoConnect for Wi-Fi configuration
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;

void rootPage() {
    Server.send(200, "text/plain", "ESP32 AutoConnect Setup");
}

void processTelemetry(){



  TelemetryData data;
  while (dequeue(data)) {
    if (start_milli_time == 0) {
      start_milli_time = data.start_milli_time;
    }

    unsigned long long delta = data.start_milli_time - start_milli_time;
    unsigned long long actual_time_stamp = (start_epoch_time * 1000ULL) + delta;

    String payload = "{";
    payload += "\"ts\": ";
    char buffer[20];
    sprintf(buffer, "%llu", actual_time_stamp);
    payload += buffer;
    payload += ",";
    payload += "\"values\":{";
    payload += "\"SPo2\":"; payload += data.n_spo2; payload += ",";
    payload += "\"PPG_R\":"; payload += data.PPG_R; payload += ",";
    payload += "\"PPG_IR\":"; payload += data.PPG_IR; payload += ",";
    payload += "\"Pulse rate\":"; payload += data.n_heart_rate;
    payload += "}}";

    // Serial.println(payload); // For debug

    // Send to ThingsBoard
    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);
    size_t json_size = measureJson(doc);
    Serial.println(tb.connected());
    bool result = tb.sendTelemetryJson(doc, json_size);
    Serial.println("Got data");
  }
}

void setup() {
    Serial.begin(115200);


    WiFi.mode(WIFI_STA);
    // Configure AutoConnect
    Config.apid = "SpO2ap";
    Config.apip = IPAddress(192,168,10,101);
    Config.autoReconnect = true;
    Config.retainPortal = false;
    Config.autoRise = true;
    Config.immediateStart = true;
    Config.hostName = "esp32-01";
    Config.channel = 6;
    Portal.config(Config);
    Server.on("/", rootPage);

    // Start Wi-Fi AutoConnect
   
    if (Portal.begin()) {
        Serial.println("WiFi connected: " + WiFi.localIP().toString());


        timeClient.begin();
        timeClient.update();
        start_epoch_time = timeClient.getEpochTime();
        int channel = WiFi.channel();
        Serial.println(channel); // see what channels
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);  // 🔧 Force channel lock

        if (WiFi.status() == WL_CONNECTED) {
        esp_wifi_set_ps(WIFI_PS_NONE); // turn off power saving
          if (esp_now_init() != ESP_OK) {
            Serial.println("ESP-NOW init failed");
            return;
          }
          esp_now_register_recv_cb((OnDataRecv));
        }
        // WiFi.mode(WIFI_STA);
      // Init ESP-NOW
      // Once ESPNow is successfully Init, we will register for recv CB to
      // get recv packer info
    } else {
        Serial.println("Failed to connect to WiFi.");
    }
}



void loop() {
    Portal.handleClient(); // Handle Wi-Fi AutoConnect portal
    processTelemetry();
    // Ensure MQTT Connection
    if (!tb.connected()) {
        Serial.println("Reconnecting to ThingsBoard...");
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN)) {
            Serial.println("Failed to connect to ThingsBoard!");
            return;
        }
    }
    tb.loop(); // Maintain MQTT connection
}