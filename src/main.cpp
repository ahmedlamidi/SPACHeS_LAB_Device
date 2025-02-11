#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "SPIFFS.h"

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

// BLE Device Info
#define SERVICE_UUID        "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcdef01-1234-5678-1234-56789abcdef0"

BLEClient *pClient;
BLERemoteCharacteristic *pRemoteCharacteristic;
bool connectedBLE = false;
uint32_t lastReceivedValue = 0;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(BLEUUID(SERVICE_UUID))) {
            Serial.println("Found target BLE device, connecting...");
            BLEDevice::getScan()->stop();
            pClient = BLEDevice::createClient();
            pClient->connect(&advertisedDevice);
            connectedBLE = true;
            BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID); 

  if (pRemoteService != nullptr) { 

    pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID); 

    if (pRemoteCharacteristic != nullptr) { 

      // Enable notifications 

      pRemoteCharacteristic->registerForNotify([](BLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) { 

        Serial.print("Received: "); 

        Serial.println((char*)pData); // Print the received message 

      }); 
        }
    }}}
};

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

    // Initialize BLE
    BLEDevice::init("ESP32_Receiver");
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);

    Serial.println("Scanning for BLE devices...");
    while (!connectedBLE) {
        pBLEScan->start(5, false);  // Keep scanning every 5 seconds
        delay(1000);
    }
    Serial.println("BLE device connected!");
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

    // Send telemetry data (example with a placeholder value)
    int receivedValue = random(90, 100);  // Simulating a SpO2 reading
    Serial.print("Sending telemetry: ");
    Serial.println(receivedValue);
    

    tb.loop(); // Maintain MQTT connection
    delay(5000);
}