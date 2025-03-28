#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
// #include <BLEDevice.h>
// #include <BLEScan.h>
// #include <BLEAdvertisedDevice.h>

// Structure example to receive data
// Must match the sender structure
typedef struct Data {
    int32_t n_spo2;  //SPO2 value
    int8_t ch_spo2_valid;  //indicator to show if the SPO2 calculation is valid
    int32_t n_heart_rate; //heart rate value
    int8_t  ch_hr_valid;  //indicator to show if the heart rate calculation is valid
    uint16_t PPG_R;
    uint16_t PPG_IR;

} message_information;

// Create a struct_message called myData
message_information myData;

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Bytes received: ");
  tb.sendTelemetryData("SPo2", myData.n_spo2);
  tb.sendTelemetryData("PPG_R", myData.PPG_R);
  tb.sendTelemetryData("PPG_IR", myData.PPG_IR);
  tb.sendTelemetryData("Pulse rate", myData.n_heart_rate);
}


constexpr char THINGSBOARD_SERVER[] = "131.247.15.226";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr char TOKEN[] = "spo2_123";
constexpr uint16_t MAX_MESSAGE_SIZE = 128U;

// MQTT and ThingsBoard objects
WiFiClient espClient;
Arduino_MQTT_Client mqttClient(espClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

// Web Server and AutoConnect for Wi-Fi configuration
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;

#define NTP_OFFSET   0 * 60      // In seconds
#define NTP_INTERVAL 5 * 1000    // In milliseconds
#define NTP_ADDRESS  "pool.ntp.org"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

void rootPage() {
    Server.send(200, "text/plain", "ESP32 AutoConnect Setup");
}

void setup() {
    Serial.begin(115200);
    
    // Configure AutoConnect
    Config.apid = "SpO2ap";
    Config.apip = IPAddress(192,168,10,101);
    Config.autoReconnect = true;
    Config.retainPortal = true;
    Config.autoRise = true;
    Config.immediateStart = true;
    Config.hostName = "esp32-01";
    Portal.config(Config);
    Server.on("/", rootPage);

    // Start Wi-Fi AutoConnect
    if (Portal.begin()) {
        Serial.println("WiFi connected: " + WiFi.localIP().toString());
    } else {
        Serial.println("Failed to connect to WiFi.");
    }

    // Start NTP Client
    timeClient.begin();


      // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
    Portal.handleClient(); // Handle Wi-Fi AutoConnect portal
    
    // Ensure MQTT Connection
    if (!tb.connected()) {
        Serial.println("Reconnecting to ThingsBoard...");
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN)) {
            Serial.println("Failed to connect to ThingsBoard!");
            return;
        }
    }
    
    tb.loop(); // Maintain MQTT connection
    delay(5000);
}