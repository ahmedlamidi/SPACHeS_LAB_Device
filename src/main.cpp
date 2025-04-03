#include <SPI.h>
#include <Arduino.h>
#define SPI_MISO    12  // Adjust based on PCB
#define SPI_MOSI    13  // Adjust based on PCB
#define SPI_SCK     14  // Adjust based on PCB
#define SPI_CS      15  // Adjust based on PCB
#define ADC_RDY     4   // ADC Ready Pin (Interrupt)
#define AFE_PDN     2   // Power Down Pin
#define AFE_RESET   0   // Reset Pin

//Pin declarations
const int SPISTE = 15;  // chip select - IO15
const int SPIDRDY = 4;  // data ready pin - IO4
volatile int drdy_trigger = LOW;
const int RESET = 0; // reset pin - IO0
const int PWDN = 2; // powerdown pin - IO2


//afe44xx Register definition
#define CONTROL0    0x00
#define LED2STC     0x01
#define LED2ENDC    0x02
#define LED2LEDSTC    0x03
#define LED2LEDENDC   0x04
#define ALED2STC    0x05
#define ALED2ENDC   0x06
#define LED1STC     0x07
#define LED1ENDC    0x08
#define LED1LEDSTC    0x09
#define LED1LEDENDC   0x0a
#define ALED1STC    0x0b
#define ALED1ENDC   0x0c
#define LED2CONVST    0x0d
#define LED2CONVEND   0x0e
#define ALED2CONVST   0x0f
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
#define ADCRSTENDCT2  0x1a
#define ADCRSTCNT3    0x1b
#define ADCRSTENDCT3  0x1c
#define PRPCOUNT    0x1d
#define CONTROL1    0x1e
#define SPARE1      0x1f
#define TIAGAIN     0x20
#define TIA_AMB_GAIN  0x21
#define LEDCNTRL    0x22
#define CONTROL2    0x23
#define SPARE2      0x24
#define SPARE3      0x25
#define SPARE4      0x26
#define SPARE4      0x26
#define RESERVED1   0x27
#define RESERVED2   0x28
#define ALARM     0x29
#define LED2VAL     0x2a
#define ALED2VAL    0x2b
#define LED1VAL     0x2c
#define ALED1VAL    0x2d
#define LED2ABSVAL    0x2e
#define LED1ABSVAL    0x2f
#define DIAG      0x30
#define count 60

void afe44xxInit (void);
void afe44xxWrite (uint8_t address, uint32_t data);
unsigned long afe44xxRead (uint8_t address);

volatile int afe44xx_data_ready = false;


///////// Gets Fired on DRDY event/////////////////////////////
ICACHE_RAM_ATTR void afe44xx_drdy_event()
{
    drdy_trigger = HIGH;
}


void afe44xxInit (void)
{
    //  Serial.println("afe44xx Initialization Starts");
    afe44xxWrite(CONTROL0, 0x000000);

    afe44xxWrite(CONTROL0, 0x000008);

    afe44xxWrite(TIAGAIN, 0x000000); // CF = 5pF, RF = 500kR
    afe44xxWrite(TIA_AMB_GAIN, 0x000001);

    afe44xxWrite(LEDCNTRL, 0x001414);
    afe44xxWrite(CONTROL2, 0x000000); // LED_RANGE=100mA, LED=50mA
    afe44xxWrite(CONTROL1, 0x010707); // Timers ON, average 3 samples

    afe44xxWrite(PRPCOUNT, 0X001F3F);

    afe44xxWrite(LED2STC, 0X001770);
    afe44xxWrite(LED2ENDC, 0X001F3E);
    afe44xxWrite(LED2LEDSTC, 0X001770);
    afe44xxWrite(LED2LEDENDC, 0X001F3F);
    afe44xxWrite(ALED2STC, 0X000000);
    afe44xxWrite(ALED2ENDC, 0X0007CE);
    afe44xxWrite(LED2CONVST, 0X000002);
    afe44xxWrite(LED2CONVEND, 0X0007CF);
    afe44xxWrite(ALED2CONVST, 0X0007D2);
    afe44xxWrite(ALED2CONVEND, 0X000F9F);

    afe44xxWrite(LED1STC, 0X0007D0);
    afe44xxWrite(LED1ENDC, 0X000F9E);
    afe44xxWrite(LED1LEDSTC, 0X0007D0);
    afe44xxWrite(LED1LEDENDC, 0X000F9F);
    afe44xxWrite(ALED1STC, 0X000FA0);
    afe44xxWrite(ALED1ENDC, 0X00176E);
    afe44xxWrite(LED1CONVST, 0X000FA2);
    afe44xxWrite(LED1CONVEND, 0X00176F);
    afe44xxWrite(ALED1CONVST, 0X001772);
    afe44xxWrite(ALED1CONVEND, 0X001F3F);

    afe44xxWrite(ADCRSTCNT0, 0X000000);
    afe44xxWrite(ADCRSTENDCT0, 0X000000);
    afe44xxWrite(ADCRSTCNT1, 0X0007D0);
    afe44xxWrite(ADCRSTENDCT1, 0X0007D0);
    afe44xxWrite(ADCRSTCNT2, 0X000FA0);
    afe44xxWrite(ADCRSTENDCT2, 0X000FA0);
    afe44xxWrite(ADCRSTCNT3, 0X001770);
    afe44xxWrite(ADCRSTENDCT3, 0X001770);

    delay(1000);
//  Serial.println("afe44xx Initialization Done");
}


void afe44xxWrite (uint8_t address, uint32_t data)
{
    digitalWrite (SS, LOW); // enable device
    SPI.transfer (address); // send address to device
    SPI.transfer ((data >> 16) & 0xFF); // write top 8 bits
    SPI.transfer ((data >> 8) & 0xFF); // write middle 8 bits
    SPI.transfer (data & 0xFF); // write bottom 8 bits
    digitalWrite (SS, HIGH); // disable device
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned long afe44xxRead (uint8_t address)
{
    unsigned long data = 0;
    digitalWrite (SS, LOW); // enable device
    SPI.transfer (address); // send address to device
    //SPI.transfer (data);
    data |= ((unsigned long)SPI.transfer (0) << 16); // read top 8 bits data
    data |= ((unsigned long)SPI.transfer (0) << 8); // read middle 8 bits  data
    data |= SPI.transfer (0); // read bottom 8 bits data
    digitalWrite (SS, HIGH); // disable device


    return data; // return with 24 bits of read data
}

void setup() {
    Serial.begin(115200);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS); // all good
    pinMode (RESET, OUTPUT); //Slave Select
    pinMode (PWDN, OUTPUT); //Slave Select

    digitalWrite(RESET, LOW);
    delay(500);
    digitalWrite(RESET, HIGH);
    delay(500);
    digitalWrite(PWDN, LOW);
    delay(500);
    digitalWrite(PWDN, HIGH);
    delay(500);
    pinMode (SPISTE, OUTPUT); //Slave Select
    pinMode (SPIDRDY, INPUT); // data ready
    attachInterrupt(SPIDRDY, afe44xx_drdy_event, FALLING); // Digital2 is attached to Data ready pin of AFE is interrupt0 in ARduino
    SPI.setClockDivider (SPI_CLOCK_DIV8); // set Speed as 2MHz , 16MHz/ClockDiv
    SPI.setDataMode (SPI_MODE1);          //Set SPI mode as 1
    SPI.setBitOrder (MSBFIRST);   
    afe44xxInit ();
}

uint8_t spi_transfer(uint8_t data) {
    digitalWrite(SS, LOW);
    uint8_t received = SPI.transfer(data);
    digitalWrite(SS, HIGH);
    return received;
}

void loop() {
    if(drdy_trigger == HIGH){
        detachInterrupt(SPIDRDY);
        afe44xxWrite(CONTROL0, 0x000001);
        int pinState = digitalRead(ADC_RDY);
        if (pinState == HIGH) {
            Serial.println("Pin is HIGH");
            } else {
            Serial.println("Pin is LOW");
            }
        unsigned long IRtemp = afe44xxRead(LED1VAL);
        afe44xxWrite(CONTROL0, 0x000001);
        Serial.println(IRtemp);
        // Print the state of the pin
        afe44xx_data_ready = false;
        drdy_trigger = LOW;
        attachInterrupt(SPIDRDY, afe44xx_drdy_event, FALLING );
    }
    else{
        Serial.println("no");
    }
}