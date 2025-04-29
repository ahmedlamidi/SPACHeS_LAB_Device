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
#include <SimpleFSM.h>   

// NTP
#define NTP_OFFSET   0 * 60
#define NTP_INTERVAL 5 * 1000
#define NTP_ADDRESS  "pool.ntp.org"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

// ESP-NOW
uint8_t broadcastAddress[] = {0x98,0xCD,0xAC,0x88,0x12,0x1C};
esp_now_peer_info_t peerInfo;

// ThingsBoard
constexpr char THINGSBOARD_SERVER[] = "131.247.15.226";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr char TOKEN[] = "spo2_123";
constexpr uint16_t MAX_MESSAGE_SIZE = 128U;
WiFiClient espClient;
Arduino_MQTT_Client mqttClient(espClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

// WebServer + AutoConnect
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;

// Telemetry queue...
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
volatile int queueHead = 0, queueTail = 0;
bool isQueueFull()  { return ((queueHead+1)%QUEUE_SIZE)==queueTail; }
bool isQueueEmpty(){ return queueHead==queueTail; }
bool enqueue(const message_information& d){
  if(isQueueFull()){ Serial.println("Queue full!"); return false; }
  telemetryQueue[queueHead]=d;
  queueHead=(queueHead+1)%QUEUE_SIZE;
  return true;
}

bool dequeue(message_information &d){
  if(isQueueEmpty()) return false;
  d = telemetryQueue[queueTail];
  queueTail=(queueTail+1)%QUEUE_SIZE;
  return true;
}

//---------------------------------------------------------------------------
// 1) Define triggers (must start at 1)
enum Triggers {
  TRG_TimeSynced = 1,
  TRG_QueueFull,
  TRG_QueueEmpty
};

// 2) Forward-declare state-entry callbacks
void onEnterTimeBroadcast();
void onEnterReceiving();
void onEnterUploading();

SimpleFSM fsm;



State states[] = {
  State("TimeBroadcast", onEnterTimeBroadcast),
  State("Receiving",      onEnterReceiving),
  State("Uploading",      onEnterUploading)
};

// 4) Define transitions (from, to, trigger)
Transition transitions[] = {
  Transition(&states[0], &states[1], TRG_TimeSynced),
  Transition(&states[1], &states[2], TRG_QueueFull),
  Transition(&states[2], &states[1], TRG_QueueEmpty),
};


void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if(len!=sizeof(unsigned long long)){
        message_information mi;
        memcpy(&mi,incomingData,sizeof(mi));
        enqueue(mi);
        if(fsm.getState()==&states[0])  fsm.trigger(TRG_TimeSynced);
        if(isQueueFull() && fsm.getState()==&states[1]){
          esp_now_send(broadcastAddress,(uint8_t*)"1",1);
          fsm.trigger(TRG_QueueFull);
        }
      }
}


// 3) Instantiate State objects
// 5) Create the FSM and wire it up

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
  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());


    timeClient.begin();
    timeClient.update();
    int channel = WiFi.channel();
    Serial.println(channel); // see what channels
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);  // channel lock

    if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_ps(WIFI_PS_NONE); // turn off power saving
      if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
      }
      esp_now_register_recv_cb((OnDataRecv));
    }
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("Failed to add peer");
        return;
    }
    // WiFi.mode(WIFI_STA);
  // Init ESP-NOW
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
} else {
    Serial.println("Failed to connect to WiFi.");
}


  // hook up FSM
  fsm.add(transitions, sizeof(transitions)/sizeof(Transition));
  fsm.setInitialState(&states[0]);
}

//---------------------------------------------------------------------------
// State entry handlers:

void onEnterTimeBroadcast(){
  unsigned long long t = timeClient.getEpochTime();
  esp_now_send(broadcastAddress, (uint8_t*)&t, sizeof(t));
}

void onEnterReceiving(){
  // no-op, data comes in via ESP-NOW callback
}

void processTelemetry(){
  message_information data;
  while(dequeue(data)){
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


void onEnterUploading(){
  processTelemetry();
}

//---------------------------------------------------------------------------

void loop() {
  Portal.handleClient();
  tb.loop();

  // let SimpleFSM handle any timed transitions
  fsm.run();

  // once upload is done, trigger back to Receiving
  if(fsm.getState()==&states[2] && isQueueEmpty()){
    esp_now_send(broadcastAddress,(uint8_t*)"2",1);
    fsm.trigger(TRG_QueueEmpty);
  }
}
