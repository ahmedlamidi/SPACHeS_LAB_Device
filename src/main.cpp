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

uint8_t broadcastAddress[] = {0x34, 0xCD, 0xB0, 0x08, 0x68, 0xA8};
// 34:CD:B0:08:68:A8
// 34:CD:B0:08:7F:D0

int current_state = 1;

// Structure example to receive data
// Must match the sender structure


constexpr char THINGSBOARD_SERVER[] = "131.247.15.226";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr char TOKEN[] = "spo2_123";
constexpr uint16_t MAX_MESSAGE_SIZE = 400U;
unsigned long long start_epoch_time;
unsigned long long start_milli_time = 0;


// MQTT and ThingsBoard objects
WiFiClient espClient;
Arduino_MQTT_Client mqttClient(espClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);


esp_now_peer_info_t peerInfo;


#define QUEUE_SIZE 200

typedef struct Data {
  int32_t n_spo2;  //SPO2 value
  int8_t ch_spo2_valid;  //indicator to show if the SPO2 calculation is valid
  int32_t n_heart_rate; //heart rate value
  int8_t  ch_hr_valid;  //indicator to show if the heart rate calculation is valid
  unsigned long long measurement_time;
  uint16_t PPG_R;
  uint16_t PPG_IR;

} message_information;

// Ring buffer
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
  if (isQueueFull()) {Serial.println("full");return false;}
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


// Create a struct_message called myData
message_information myData;

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    current_state = 1;
  memcpy(&myData, incomingData, sizeof(message_information));
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

#define BATCH_SIZE 8 // Send up to 8 readings per message
#define MAX_JSON_BUFFER_SIZE 1500 // Adjust if needed for larger batches

void processTelemetry() {
    if (isQueueEmpty()) {
        return; // Nothing to do
    }

    // Prepare JSON array string
    String payload = "[";
    int itemsInBatch = 0;
    DynamicJsonDocument doc(MAX_JSON_BUFFER_SIZE); // Use one doc for the whole batch potentially? - Requires Careful implementation or build string manually.
    // Safer: Build string manually to avoid complex JSON manipulation for arrays

    message_information data;
    while (itemsInBatch < BATCH_SIZE && dequeue(data)) {
        if (itemsInBatch > 0) {
            payload += ","; // Add comma between objects
        }
        payload += "{\"ts\": ";
        payload += (unsigned long long)data.measurement_time; // Cast for String concat if needed
        payload += ", \"values\": {";
        payload += "\"SPo2\":"; payload += data.n_spo2; payload += ",";
        payload += "\"PPG_R\":"; payload += data.PPG_R; payload += ",";
        payload += "\"PPG_IR\":"; payload += data.PPG_IR; payload += ",";
        payload += "\"Pulse rate\":"; payload += data.n_heart_rate;
        payload += "}}";
        itemsInBatch++;
    }
    payload += "]"; // Close JSON array
    if (itemsInBatch > 0) {

        DynamicJsonDocument doc(1500);
        deserializeJson(doc, payload);
        size_t json_size = measureJson(doc);

        // Check connection and send the batch
        if (!tb.connected()) {
            Serial.println("Reconnecting to ThingsBoard...");
            if (!tb.connect(THINGSBOARD_SERVER, TOKEN)) {
                Serial.println("Failed to connect to ThingsBoard!");
                // Error Handling: Consider re-queuing failed batch data? Difficult.
                return;
            } else {
                Serial.print("Sending batch data... ");
                bool result = tb.sendTelemetryJson(doc, json_size);
                Serial.println(result ? "OK" : "Failed");
            }
        } else {
            Serial.print("Sending batch data... ");
            bool result = tb.sendTelemetryJson(doc, json_size);
            Serial.println(result ? "OK" : "Failed");
        }
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

          current_state = 0;
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
}



void loop() {
    if(current_state == 0){ 
        start_epoch_time = timeClient.getEpochTime();
        esp_now_send(broadcastAddress, (uint8_t *)&start_epoch_time, sizeof(start_epoch_time));
    }

    Portal.handleClient(); // Handle Wi-Fi AutoConnect portal
    processTelemetry();
    tb.loop(); // Maintain MQTT connection
}