/////////////////////////////////////////////////////////////////////////////////////////
//
//   AFE44xx Arduino Firmware
//   (with SimpleFSM-based ESP-NOW sender state machine)
//
//   Copyright (c) 2016 ProtoCentral
//   License: MIT
//   https://github.com/Protocentral/afe44xx_Oximeter
/////////////////////////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include <SimpleFSM.h>
#include <ArduinoJson.h>
#include <cmath>

// ─── AFE44xx REGISTER DEFINITIONS ────────────────────────────────────────────
#define CONTROL0      0x00
#define LED2STC       0x01
#define LED2ENDC      0x02
#define LED2LEDSTC    0x03
#define LED2LEDENDC   0x04
#define ALED2STC      0x05
#define ALED2ENDC     0x06
#define LED1STC       0x07
#define LED1ENDC      0x08
#define LED1LEDSTC    0x09
#define LED1LEDENDC   0x0A
#define ALED1STC      0x0B
#define ALED1ENDC     0x0C
#define LED2CONVST    0x0D
#define LED2CONVEND   0x0E
#define ALED2CONVST   0x0F
#define ALED2CONVEND  0x10
#define LED1CONVST    0x11
#define LED1CONVEND   0x12
#define ALED1CONVST   0x13
#define ALED1CONVEND  0x14
#define ADCRSTCNT0    0x15
#define ADCRSTENDCT0  0x16
#define ADCRSTCNT1    0x17
#define ADCRSTENDCT1  0x18
#define ADCRSTCNT2    0x19
#define ADCRSTENDCT2  0x1A
#define ADCRSTCNT3    0x1B
#define ADCRSTENDCT3  0x1C
#define PRPCOUNT      0x1D
#define CONTROL1      0x1E
#define TIAGAIN       0x20
#define TIA_AMB_GAIN  0x21
#define LEDCNTRL      0x22
#define CONTROL2      0x23
#define ALARM         0x29
#define LED2VAL       0x2A
#define ALED2VAL      0x2B
#define LED1VAL       0x2C
#define ALED1VAL      0x2D
#define DIAG          0x30

// ─── PIN DEFINITIONS ─────────────────────────────────────────────────────────
const int SPISTE    = 15;   // AFE44xx chip-select (CS)
const int SPIDRDY   = 4;    // Data-ready interrupt from AFE44xx
const int RESET_PIN = 0;    // AFE44xx RESET
const int PWDN_PIN  = 2;    // AFE44xx PWDN
#define GRN_LED     27
#define RED_LED     26
#define BATTERY_IN  39
#define CHARGER     18

// ─── ESP-NOW PEER ────────────────────────────────────────────────────────────
uint8_t receiverAddress[] = {0x34,0xCD,0xB0,0x08,0x68,0xA8};
esp_now_peer_info_t peerInfo;

// ─── TELEMETRY TYPES & BUFFERS ───────────────────────────────────────────────
enum MsgCmd : uint8_t { SYNC_TIME = 1, DATA = 2, WAIT = 3, READY = 4 };
struct TelemetryData {
  int32_t  n_spo2;
  int8_t   ch_spo2_valid;
  int32_t  n_heart_rate;
  int8_t   ch_hr_valid;
  uint64_t measurement_time;
  uint16_t PPG_R;
  uint16_t PPG_IR;
};

union Payload {
  uint64_t      timestamp;
  TelemetryData telemetry[6];
};

struct Message {
  MsgCmd  cmd;
  Payload payload;
};
TelemetryData sendBuffer[6];
uint8_t     bufferIndex = 0;

// ─── AFE44xx SAMPLE BUFFERS ──────────────────────────────────────────────────
#define FS           25
#define BUFFER_SIZE (FS*4)
#define MA4_SIZE      4
uint16_t aun_ir_buffer[100], aun_red_buffer[100];
uint64_t time_stamps[100];
volatile bool     drdy_trigger    = false;
volatile uint16_t n_buffer_count  = 0;
int32_t            calculated_SpO2;
int8_t             valid_flag;
int32_t            calculated_HR;

// ─── TIME SYNC ───────────────────────────────────────────────────────────────
uint64_t start_epoch_time, start_milli_time;

// ─── NTP CLIENT ─────────────────────────────────────────────────────────────
WiFiUDP    ntpUDP;
NTPClient  timeClient(ntpUDP, "pool.ntp.org", 0, 5000);

// ─── WEBSERVER & THINGSBOARD (unused here) ─────────────────────────────────
WebServer            Server;
AutoConnect          Portal(Server);
AutoConnectConfig    Config;
WiFiClient           espClient;
Arduino_MQTT_Client  mqttClient(espClient);
ThingsBoard          tb(mqttClient, 128);

// ─── SIMPLEFSM SENDER SETUP ─────────────────────────────────────────────────
void onEnterTimeSync();
void onEnterTransmit();
void onEnterWaitStore();

State fsmStates[] = {
  State("TimeSync",  onEnterTimeSync),
  State("Transmit",  onEnterTransmit),
  State("WaitStore", onEnterWaitStore)
};
enum Triggers { TRG_TimeReceived = 1, TRG_WaitReceived, TRG_ReadyReceived };
Transition fsmTransitions[] = {
  Transition(&fsmStates[0], &fsmStates[1], TRG_TimeReceived),
  Transition(&fsmStates[1], &fsmStates[2], TRG_WaitReceived),
  Transition(&fsmStates[2], &fsmStates[1], TRG_ReadyReceived)
};
SimpleFSM fsm;

// ─── FORWARD DECLARATIONS ────────────────────────────────────────────────────
void afe44xxInit();
void afe44xxWrite(uint8_t addr, uint32_t data);
uint32_t afe44xxRead(uint8_t addr);
void sort_ascend(int32_t*, int);
void find_peak_above(int32_t*,int32_t*,int32_t*,int,int);
void sort_indices_descend(int32_t*,int32_t*,int);
void remove_close_peaks(int32_t*,int32_t*,int32_t*,int);
void find_peak(int32_t*,int32_t*,int32_t*,int,int,int,int);
void estimate_spo2(uint16_t*,int,uint16_t*,int32_t*,int8_t*,int32_t*,int8_t*,uint64_t*);
TelemetryData getCurrentTelemetry();
void transmitData(), bufferData();

// ─── ESP-NOW RECEIVE CALLBACK ───────────────────────────────────────────────
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Message msg;
  memcpy(&msg, incomingData, len);
  switch (msg.cmd) {
    case SYNC_TIME:
      start_epoch_time = msg.payload.timestamp;
      start_milli_time = millis();
      fsm.trigger(TRG_TimeReceived);
      break;
    case WAIT:
      fsm.trigger(TRG_WaitReceived);
      break;
    case READY:
      fsm.trigger(TRG_ReadyReceived);
      break;
    default: break;
  }
}

// ─── AFE44xx DRDY INTERRUPT ─────────────────────────────────────────────────
ICACHE_RAM_ATTR void IRAM_ATTR afe44xx_drdy_event() {
  drdy_trigger = true;
}

// ─── SETUP ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(GRN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BATTERY_IN, INPUT);
  pinMode(CHARGER, OUTPUT);
  digitalWrite(RED_LED, HIGH);

  // ESP-NOW

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE); // change to match receiver channel
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  }
  esp_now_register_recv_cb(OnDataRecv);
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
}


  // SPI + AFE44xx
  pinMode(RESET_PIN, OUTPUT);
  pinMode(PWDN_PIN, OUTPUT);
  pinMode(SPISTE, OUTPUT);
  pinMode(SPIDRDY, INPUT_PULLUP);
  SPI.begin(14,12,13,SPISTE);
  attachInterrupt(digitalPinToInterrupt(SPIDRDY), afe44xx_drdy_event, FALLING);

  // AFE44xx reset/pwdn
  digitalWrite(RESET_PIN, LOW);  delay(5);
  digitalWrite(RESET_PIN, HIGH); delay(5);
  digitalWrite(PWDN_PIN, LOW);   delay(5);
  digitalWrite(PWDN_PIN, HIGH);  delay(5);

  afe44xxInit();
  Serial.println("AFE44xx ready");
  digitalWrite(RED_LED, LOW);

  // FSM
  fsm.add(fsmTransitions,
          sizeof(fsmTransitions)/sizeof(fsmTransitions[0]));
  fsm.setInitialState(&fsmStates[0]);
}

// ─── STATE-ENTRY CALLBACKS ──────────────────────────────────────────────────
void onEnterTimeSync() {
  Message m; m.cmd = SYNC_TIME; m.payload.timestamp = 0;
  esp_now_send(receiverAddress,(uint8_t*)&m,sizeof(m));
}

void onEnterTransmit()  { transmitData(); }
void onEnterWaitStore() { /* buffer until READY */ }

// ─── LOOP ────────────────────────────────────────────────────────────────────
void loop() {
  fsm.run();
  Serial.println(fsm.getState()->getName());

  // blink green LED
  static uint32_t last = 0;
  static bool on = false;
  if (millis() - last > 1000) {
    on = !on;
    digitalWrite(GRN_LED, on);
    last = millis();
  }

  // handle AFE44xx samples
  if (drdy_trigger) {
    drdy_trigger = false;
    uint32_t rIR  = afe44xxRead(LED1VAL),
             rRED = afe44xxRead(LED2VAL);
    int32_t ir  = ((int32_t)(rIR  <<  8)) >> 12;
    int32_t red = ((int32_t)(rRED <<  8)) >> 12;
    aun_ir_buffer[n_buffer_count]  = ir;
    aun_red_buffer[n_buffer_count] = red;
    time_stamps[n_buffer_count]    = millis();
    if (++n_buffer_count >= 100) {
      estimate_spo2(aun_ir_buffer,100,
                    aun_red_buffer,
                    &calculated_SpO2,
                    &valid_flag,
                    &calculated_HR,
                    &valid_flag,
                    time_stamps);
      n_buffer_count = 0;
    }
  }

  // state behavior
  if (fsm.getState() == &fsmStates[1]) {
    Serial.println("state 1");
    transmitData();
  } else if (fsm.getState() == &fsmStates[2]) {
    bufferData();
    Serial.println("state 2");
  }
}

// ─── TELEMETRY HELPERS ──────────────────────────────────────────────────────
TelemetryData getCurrentTelemetry() {
  TelemetryData t;
  t.n_spo2           = calculated_SpO2;
  t.ch_spo2_valid    = valid_flag;
  t.n_heart_rate     = calculated_HR;
  t.ch_hr_valid      = valid_flag;
  t.measurement_time = start_epoch_time*1000ULL + (millis()-start_milli_time);
  t.PPG_R            = aun_red_buffer[n_buffer_count-1];
  t.PPG_IR           = aun_ir_buffer[n_buffer_count-1];
  return t;
}

void transmitData() {
  Message m;
  m.cmd = DATA;
  sendBuffer[bufferIndex++] = getCurrentTelemetry();
  if (bufferIndex >= 6) {
    memcpy(m.payload.telemetry, sendBuffer, sizeof(sendBuffer));
    esp_err_t status = esp_now_send(receiverAddress,(uint8_t*)&m,sizeof(m));
    if (status == ESP_OK){
      Serial.println("Sent Data");
    }
    else{
      Serial.println("Send Failed");
      Serial.println(sizeof(m));
      Serial.println(bufferIndex);
    }
    bufferIndex = 0;
  }
}

void bufferData() {
  sendBuffer[bufferIndex++] = getCurrentTelemetry();
}

// ─── AFE44xx I/O ────────────────────────────────────────────────────────────
void afe44xxInit() {
  afe44xxWrite(CONTROL0,     0x000000);
  afe44xxWrite(CONTROL0,     0x000008);
  afe44xxWrite(TIAGAIN,      0x000000);
  afe44xxWrite(TIA_AMB_GAIN, 0x000001);
  afe44xxWrite(LEDCNTRL,     0x001414);
  afe44xxWrite(CONTROL2,     0x000000);
  afe44xxWrite(CONTROL1,     0x010707);
  afe44xxWrite(PRPCOUNT,     0x001F3F);
  afe44xxWrite(LED2STC,      0x001770);
  afe44xxWrite(LED2ENDC,     0x001F3E);
  afe44xxWrite(LED2LEDSTC,   0x001770);
  afe44xxWrite(LED2LEDENDC,  0x001F3F);
  afe44xxWrite(ALED2STC,     0x000000);
  afe44xxWrite(ALED2ENDC,    0x0007CE);
  afe44xxWrite(LED2CONVST,   0x000002);
  afe44xxWrite(LED2CONVEND,  0x0007CF);
  afe44xxWrite(ALED2CONVST,  0x0007D2);
  afe44xxWrite(ALED2CONVEND, 0x000F9F);
  afe44xxWrite(LED1STC,      0x0007D0);
  afe44xxWrite(LED1ENDC,     0x000F9E);
  afe44xxWrite(LED1LEDSTC,   0x0007D0);
  afe44xxWrite(LED1LEDENDC,  0x000F9F);
  afe44xxWrite(ALED1STC,     0x000FA0);
  afe44xxWrite(ALED1ENDC,    0x00176E);
  afe44xxWrite(LED1CONVST,   0x000FA2);
  afe44xxWrite(LED1CONVEND,  0x00176F);
  afe44xxWrite(ALED1CONVST,  0x001772);
  afe44xxWrite(ALED1CONVEND, 0x001F3F);
  afe44xxWrite(ADCRSTCNT0,   0x000000);
  afe44xxWrite(ADCRSTENDCT0, 0x000000);
  afe44xxWrite(ADCRSTCNT1,   0x0007D0);
  afe44xxWrite(ADCRSTENDCT1, 0x0007D0);
  afe44xxWrite(ADCRSTCNT2,   0x000FA0);
  afe44xxWrite(ADCRSTENDCT2, 0x000FA0);
  afe44xxWrite(ADCRSTCNT3,   0x001770);
  afe44xxWrite(ADCRSTENDCT3, 0x001770);
  delay(100);
}

void afe44xxWrite(uint8_t addr, uint32_t data) {
  digitalWrite(SPISTE, LOW);
  SPI.transfer(addr);
  SPI.transfer((data>>16)&0xFF);
  SPI.transfer((data>>8)&0xFF);
  SPI.transfer(data&0xFF);
  digitalWrite(SPISTE, HIGH);
}

uint32_t afe44xxRead(uint8_t addr) {
  uint32_t r=0;
  digitalWrite(SPISTE, LOW);
  SPI.transfer(addr);
  r  = ((uint32_t)SPI.transfer(0)<<16);
  r |= ((uint32_t)SPI.transfer(0)<<8);
  r |=  SPI.transfer(0);
  digitalWrite(SPISTE, HIGH);
  return r;
}

// ─── SPO₂ ESTIMATION ROUTINES ────────────────────────────────────────────────
void sort_ascend(int32_t *x, int n) {
  for(int i=1;i<n;++i){
    int32_t v=x[i], j=i;
    while(j>0 && v<x[j-1]){ x[j]=x[j-1]; --j; }
    x[j]=v;
  }
}

void find_peak_above(int32_t *locs,int32_t *npk,int32_t *x,int n,int minh){
  *npk=0; int i=1;
  while(i<n-1){
    if(x[i]>minh && x[i]>x[i-1]){
      int w=1; while(i+w<n && x[i]==x[i+w]) w++;
      if(i+w<n && x[i]>x[i+w] && *npk<15){
        locs[(*npk)++]=i; i+=w+1;
      } else i+=w;
    } else i++;
  }
}

void sort_indices_descend(int32_t *x,int32_t *ind,int n){
  for(int i=1;i<n;++i){
    int t=ind[i], j=i;
    while(j>0 && x[t]>x[ind[j-1]]){ ind[j]=ind[j-1]; --j; }
    ind[j]=t;
  }
}

void remove_close_peaks(int32_t *locs,int32_t *npk,int32_t *x,int mind){
  int old=*npk;
  sort_indices_descend(x,locs,*npk);
  int out=0;
  for(int i=0;i<old;++i){
    bool keep=true;
    for(int j=0;j<out;++j){
      if(abs(locs[i]-locs[j])<mind){ keep=false; break; }
    }
    if(keep) locs[out++]=locs[i];
  }
  *npk=out;
  sort_ascend(locs,out);
}

void find_peak(int32_t *locs,int32_t *npk,int32_t *x,int n,int minh,int mind,int maxn){
  find_peak_above(locs,npk,x,n,minh);
  remove_close_peaks(locs,npk,x,mind);
  if(*npk>maxn)*npk=maxn;
}

const uint8_t uch_spo2_table[184]={95,95,95,96,96,96,97,97,97,97,97,98,98,98,98,98,99,99,99,99,
  99,99,99,99,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,
  100,100,100,100,99,99,99,99,99,99,99,99,98,98,98,98,98,98,97,97,
  97,97,96,96,96,96,95,95,95,94,94,94,93,93,93,92,92,92,91,91,
  90,90,89,89,89,88,88,87,87,86,86,85,85,84,84,83,82,82,81,81,
  80,80,79,78,78,77,76,76,75,74,74,73,72,72,71,70,69,69,68,67,
  66,66,65,64,63,62,62,61,60,59,58,57,56,56,55,54,53,52,51,50,
  49,48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33,31,30,29,
  28,27,26,25,23,22,21,20,19,17,16,15,14,12,11,10,9,7,6,5,3,2,1};

void estimate_spo2(uint16_t *irbuf,int len,uint16_t *redbuf,
                   int32_t *spo2,int8_t *spov,int32_t *hr,int8_t *hrv,
                   uint64_t *ts){
  // DC removal + moving avg, valley detection, HR calc
  int32_t x[BUFFER_SIZE], y[BUFFER_SIZE];
  uint32_t sum=0;
  for(int i=0;i<len;++i) sum+=irbuf[i];
  uint32_t mean=sum/len;
  for(int i=0;i<len;++i) x[i]=-(int32_t)(irbuf[i]-mean);
  for(int i=0;i<BUFFER_SIZE-MA4_SIZE;++i)
    x[i]=(x[i]+x[i+1]+x[i+2]+x[i+3])/4;
  int32_t th=0;
  for(int i=0;i<BUFFER_SIZE;++i) th+=x[i];
  th/=BUFFER_SIZE; th=min(max(th,30),60);
  int32_t locs[15], npk;
  find_peak(locs,&npk,x,BUFFER_SIZE,th,4,15);
  if(npk>=2){
    int sumt=0;
    for(int i=1;i<npk;++i)
      sumt+=ts[locs[i]]-ts[locs[i-1]];
    *hr=60000/(sumt/(npk-1));
    *hrv=1; calculated_HR=*hr;
  } else {*hr=-999; *hrv=0; valid_flag=0;}
  // SPO2
  for(int i=0;i<len;++i){ x[i]=irbuf[i]; y[i]=redbuf[i]; }
  int ratios[5], rc=0;
  for(int k=0;k<npk-1;++k){
    int start=locs[k], end=locs[k+1];
    if(end-start<4) continue;
    int xdc=-1e9, ydc=-1e9, xi=0, yi=0;
    for(int i=start;i<=end;++i){
      if(x[i]>xdc){ xdc=x[i]; xi=i; }
      if(y[i]>ydc){ ydc=y[i]; yi=i; }
    }
    float xr   = (float)(x[end]-x[start])*(xi-start)/(end-start) + x[start];
    float yr   = (float)(y[end]-y[start])*(yi-start)/(end-start) + y[start];
    float xac  = x[xi]-xr, yac=y[yi]-yr;
    if(xdc>0 && ydc>0){
      int r = (int)((yac*(float)xdc)/(xac*(float)ydc)*100);
      if(r>2 && r<184 && rc<5) ratios[rc++]=r;
    }
  }
  sort_ascend(ratios,rc);
  int mid=rc/2;
  int rav= (rc>1)?(ratios[mid-1]+ratios[mid])/2:ratios[mid];
  if(rav>2 && rav<184){
    *spo2=uch_spo2_table[rav];
    *spov=1; calculated_SpO2=*spo2;
  } else {*spo2=-999; *spov=0; calculated_SpO2=-999;}
}

// ─── END OF SKETCH ───────────────────────────────────────────────────────────
