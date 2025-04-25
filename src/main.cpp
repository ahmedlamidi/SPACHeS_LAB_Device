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



//NTP library to get real time from server
#define NTP_OFFSET   0 * 60      // In seconds
#define NTP_INTERVAL 5 * 1000    // In miliseconds
#define NTP_ADDRESS  "pool.ntp.org"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

uint8_t broadcastAddress[] = {0x98, 0xCD, 0xAC, 0x88, 0x12, 0x1C};
esp_now_peer_info_t peerInfo;

#define MAX_BUFFERED_ROWS 20
String csvBuffer = "";
int bufferedCount = 0;
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





#define QUEUE_SIZE 240

struct TelemetryData {
  int32_t n_spo2;  //SPO2 value
  int8_t ch_spo2_valid;  //indicator to show if the SPO2 calculation is valid
  int32_t n_heart_rate; //heart rate value
  int8_t  ch_hr_valid;  //indicator to show if the heart rate calculation is valid
  unsigned long long measurement_time;
  uint16_t PPG_R;
  uint16_t PPG_IR;
};

enum Command : uint8_t {SYNC_TIME, READY, WAIT, DATA};

struct Message {
  Command cmd;
  union {
    uint64_t timestamp;
    TelemetryData telemetry[12];
  } payload;
};

// Ring buffer
TelemetryData telemetryQueue[QUEUE_SIZE];
volatile int queueHead = 0;
volatile int queueTail = 0;
unsigned long lastDataReceived = 0;
const unsigned long DATA_TIMEOUT_MS = 60000; // 60 sec


int current_state = 0;

bool isQueueFull() {
  return ((queueHead + 1) % QUEUE_SIZE) == queueTail;
}

bool isQueueEmpty() {
  return queueHead == queueTail;
}

bool enqueue(const TelemetryData& data) {
  if (isQueueFull()) {Serial.println("full");return false;}
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


void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Message msg;
  memcpy(&msg, incomingData, sizeof(Message));
  if (msg.cmd == DATA) {
    for(int i=0; i < ((len - sizeof(Command))/sizeof(TelemetryData)); i++) {
      enqueue(msg.payload.telemetry[i]);
    }
    current_state = 1;
    lastDataReceived = millis();
  }
}

void broadcastTimestamp() {
  Message msg;
  msg.cmd = SYNC_TIME;
  timeClient.update();
  msg.payload.timestamp = timeClient.getEpochTime();
  esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(Command) + sizeof(uint64_t));
}

void sendCommand(Command cmd) {
  Message msg = {.cmd = cmd};
  esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(Command));
}


// Web Server and AutoConnect for Wi-Fi configuration
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;

void rootPage() {
    Server.send(200, "text/plain", "ESP32 AutoConnect Setup");
}


void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

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

  if(Portal.begin()){
    timeClient.begin();
    esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
    if(WiFi.status() == WL_CONNECTED){
      esp_wifi_set_ps(WIFI_PS_NONE); 
      if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
      }
      esp_now_register_recv_cb(OnDataRecv);
  }
    
  

    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK){
      Serial.println("Failed to add peer");
      return;
  }

  }
  

}

void uploadTelemetry() {
  TelemetryData data;
  while (dequeue(data)) {
    String payload = "{\"ts\":" + String(data.measurement_time) + ",\"values\":{\"SpO2\":" + data.n_spo2 + ",\"Pulse\":" + data.n_heart_rate + ",\"PPG_R\":" + data.PPG_R + ",\"PPG_IR\":" + data.PPG_IR + "}}";
    DynamicJsonDocument doc(1500);
        deserializeJson(doc, payload);
        size_t json_size = measureJson(doc);
        Serial.println(tb.connected());
        if (!tb.connected()) {
            Serial.println("Reconnecting to ThingsBoard...");
            if (!tb.connect(THINGSBOARD_SERVER, TOKEN)) {
                Serial.println("Failed to connect to ThingsBoard!");
                return;
            }
            else{
                bool result = tb.sendTelemetryJson(doc, json_size);
                Serial.println(result);
            }
        }
        else{
          bool result = tb.sendTelemetryJson(doc, json_size);
                Serial.println(result);
        }
  }
}

void loop() {
  Portal.handleClient();
  tb.loop();

  if (current_state == 1 && (millis() - lastDataReceived > DATA_TIMEOUT_MS)) {
    current_state = 0;
    Serial.println("Timeout. Broadcasting time...");
  }

  if (current_state == 0) broadcastTimestamp();

  if ((queueHead - queueTail + QUEUE_SIZE) % QUEUE_SIZE >= 100) {
    sendCommand(WAIT);
    uploadTelemetry();
    sendCommand(READY);
  }
}