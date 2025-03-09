#include <DFRobot_B_LUX_V30B.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

// Ambient light sensor using DFRobot SEN0390.
// Although the constructor expects an "EN" pin, if EN is tied to 5V the sensor is always enabled.
// Use any available digital pin (here we use 13). SCL and SDA default to the hardware I2C (A5 and A4 on the Arduino Uno).
DFRobot_B_LUX_V30B myLux(13);

// LoRaWAN keys and identifiers
#ifdef COMPILE_REGRESSION_TEST
  #define FILLMEIN 0
#else
  #warning "Replace FILLMEIN values with your TTN keys!"
  #define FILLMEIN (#dont edit this, edit the lines that use FILLMEIN)
#endif

static const u1_t PROGMEM APPEUI[8] = { 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
};
void os_getArtEui (u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

static const u1_t PROGMEM DEVEUI[8] = { 
  0xDF, 0xE9, 0x06, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 
};
void os_getDevEui (u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}

static const u1_t PROGMEM APPKEY[16] = { 
  0xDC, 0x4E, 0x23, 0xD5, 0x28, 0x2C, 0x35, 0x19,
  0x9E, 0xFB, 0x6B, 0x4A, 0xC3, 0x7C, 0x56, 0xE8 
};
void os_getDevKey (u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

// Array to hold sensor data: 2 bytes for ambient light and 1 byte for the PIR sensor.
static uint8_t mydata[3];
static osjob_t sendjob;
const unsigned TX_INTERVAL = 10;

// LoRa module pin mapping.
const lmic_pinmap lmic_pins = {
    .nss = 10,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 7,
    .dio = {2, 5, 6},
};

// Helper function to print two-digit hexadecimal values.
void printHex2(unsigned v) {
    v &= 0xff;
    if (v < 16)
        Serial.print('0');
    Serial.print(v, HEX);
}

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            { 
              u4_t netid = 0;
              devaddr_t devaddr = 0;
              u1_t nwkKey[16];
              u1_t artKey[16];
              LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
              Serial.print("netid: ");
              Serial.println(netid, DEC);
              Serial.print("devaddr: ");
              Serial.println(devaddr, HEX);
              Serial.print("AppSKey: ");
              for (size_t i = 0; i < sizeof(artKey); ++i) {
                if (i != 0)
                  Serial.print("-");
                printHex2(artKey[i]);
              }
              Serial.println("");
              Serial.print("NwkSKey: ");
              for (size_t i = 0; i < sizeof(nwkKey); ++i) {
                if (i != 0)
                  Serial.print("-");
                printHex2(nwkKey[i]);
              }
              Serial.println();
            }
            LMIC_setLinkCheckMode(0);
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            break;
        case EV_TXCANCELED:
            Serial.println(F("EV_TXCANCELED"));
            break;
        case EV_RXSTART:
            break;
        case EV_JOIN_TXCOMPLETE:
            Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
            break;
        default:
            Serial.print(F("Unknown event: "));
            Serial.println((unsigned) ev);
            break;
    }
}

// Reads the ambient light and PIR sensor data then sends it via LoRaWAN.
void do_send(osjob_t* j) {
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Read the ambient light intensity (in lux) from the DFRobot sensor.
        float lux = myLux.lightStrengthLux();
        Serial.print(F("Ambient Light: "));
        Serial.println(lux);
        // Convert the float lux value to a 16-bit integer.
        uint16_t luxValue = (uint16_t)lux;
        mydata[0] = (luxValue >> 8) & 0xFF;
        mydata[1] = luxValue & 0xFF;

        // Read the PIR sensor value from pin D8.
        int pirValue = digitalRead(8);
        Serial.print(F("PIR Sensor reading: "));
        Serial.println(pirValue);
        mydata[2] = (uint8_t) pirValue;

        // Queue the data for transmission on LoRaWAN port 1.
        LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
        Serial.println(F("Packet queued"));
    }
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
}

void setup() {
    Serial.begin(9600);
    Serial.println(F("Starting LoRaWAN sensor transmission"));

    // Initialize the PIR sensor pin.
    pinMode(8, INPUT);

    // Initialize the ambient light sensor.
    myLux.begin();

    os_init();
    LMIC_reset();

    do_send(&sendjob);
}

void loop() {
    os_runloop_once();
}
