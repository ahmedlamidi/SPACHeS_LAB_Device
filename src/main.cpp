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

#include <fsm.h>

// ----------- NTP Setup -------------
#define NTP_OFFSET   0 * 60
#define NTP_INTERVAL 5 * 1000
#define NTP_ADDRESS  "pool.ntp.org"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

// ----------- ESP-NOW Setup ----------
uint8_t broadcastAddress[] = {0x34, 0xCD, 0xB0, 0x08, 0x68, 0xA8};

esp_now_peer_info_t peerInfo;

// ----------- ThingsBoard Setup -------
constexpr char THINGSBOARD_SERVER[] = "131.247.15.226";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr char TOKEN[] = "spo2_123";
constexpr uint16_t MAX_MESSAGE_SIZE = 128U;

WiFiClient espClient;
Arduino_MQTT_Client mqttClient(espClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

// ----------- Web Server + AutoConnect -------
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;

// ----------- Telemetry Queue ----------
#define QUEUE_SIZE 200

typedef struct Data {
  int32_t n_spo2;
  int8_t ch_spo2_valid;
  int32_t n_heart_rate;
  int8_t  ch_hr_valid;
  unsigned long long measurement_time;
  uint16_t PPG_R;
  uint16_t PPG_IR;
} message_information;

message_information telemetryQueue[QUEUE_SIZE];
volatile int queueHead = 0;
volatile int queueTail = 0;

bool isQueueFull() {
  return ((queueHead + 1) % QUEUE_SIZE) == queueTail;
}

bool isQueueEmpty() {
  return queueHead == queueTail;
}

bool enqueue(const message_information& data) {
  if (isQueueFull()) {
    Serial.println("Queue full!");
    return false;
  }
  telemetryQueue[queueHead] = data;
  queueHead = (queueHead + 1) % QUEUE_SIZE;
  return true;
}

bool dequeue(message_information &data) {
  if (isQueueEmpty()) return false;
  data = telemetryQueue[queueTail];
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  return true;
}

// ----------- FSM Setup ---------------
// Define State, Event, Action enums
enum class ReceiverState {
  TimeBroadcast,
  Receiving,
  Uploading
};

enum class ReceiverEvent {
  TimeSynced,
  QueueFull,
  QueueEmpty
};

enum class ReceiverAction {
  StartReceiving,
  StartUploading,
  ResumeReceiving
};

// Define FSM transitions
constexpr std::array<fsm::Transition<ReceiverEvent, ReceiverState, ReceiverAction>, 3> receiverTransitions{{
  {.state = ReceiverState::TimeBroadcast, .event = ReceiverEvent::TimeSynced, .action = ReceiverAction::StartReceiving, .newState = ReceiverState::Receiving},
  {.state = ReceiverState::Receiving, .event = ReceiverEvent::QueueFull, .action = ReceiverAction::StartUploading, .newState = ReceiverState::Uploading},
  {.state = ReceiverState::Uploading, .event = ReceiverEvent::QueueEmpty, .action = ReceiverAction::ResumeReceiving, .newState = ReceiverState::Receiving}
}};

// Define FSM object
auto receiverFSM = fsm::Fsm<ReceiverEvent, ReceiverState, ReceiverAction, receiverTransitions>{
  .state = ReceiverState::TimeBroadcast
};

// ----------- Variables ---------------
unsigned long long start_epoch_time;
unsigned long long start_milli_time = 0;

message_information myData;

// ----------- Functions ---------------

void rootPage() {
  Server.send(200, "text/plain", "ESP32 AutoConnect Setup");
}

void processTelemetry() {
  message_information data;
  while (dequeue(data)) {
    unsigned long long actual_time_stamp = data.measurement_time;

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

    Serial.println(payload);

    DynamicJsonDocument doc(1500);
    deserializeJson(doc, payload);
    size_t json_size = measureJson(doc);

    if (!tb.connected()) {
      Serial.println("Reconnecting to ThingsBoard...");
      if (!tb.connect(THINGSBOARD_SERVER, TOKEN)) {
        Serial.println("Failed to connect to ThingsBoard!");
        return;
      }
    }
    bool result = tb.sendTelemetryJson(doc, json_size);
    Serial.println(result);
  }
}

// --- ESP-NOW callback when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(unsigned long long)) {
    // This is a time request (sender waiting for time sync)
    return;
  }

  memcpy(&myData, incomingData, sizeof(message_information));
  enqueue(myData);

  // If we were broadcasting time, now we synced
  if (receiverFSM.state == ReceiverState::TimeBroadcast) {
    receiverFSM.input(ReceiverEvent::TimeSynced);
  }

  // If queue full, move to uploading
  if (isQueueFull() && receiverFSM.state == ReceiverState::Receiving) {
    esp_now_send(broadcastAddress, (uint8_t*)"1", sizeof(uint8_t)); // Send WAIT
    receiverFSM.input(ReceiverEvent::QueueFull);
  }
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

  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());

    timeClient.begin();
    timeClient.update();
    Serial.println(channel);
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

    if (WiFi.status() == WL_CONNECTED) {
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

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
    }
  } else {
    Serial.println("Failed to connect to WiFi.");
  }
}

void loop() {
  Portal.handleClient();
  tb.loop();

  switch (receiverFSM.state) {
    case ReceiverState::TimeBroadcast:
      start_epoch_time = timeClient.getEpochTime();
      esp_now_send(broadcastAddress, (uint8_t *)&start_epoch_time, sizeof(start_epoch_time));
      break;

    case ReceiverState::Receiving:
      // Receiving handled inside OnDataRecv
      break;

    case ReceiverState::Uploading:
      processTelemetry();
      if (isQueueEmpty()) {
        esp_now_send(broadcastAddress, (uint8_t*)"2", sizeof(uint8_t)); // Send READY
        receiverFSM.input(ReceiverEvent::QueueEmpty);
      }
      break;
  }
}