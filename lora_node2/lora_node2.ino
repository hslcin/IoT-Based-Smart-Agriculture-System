/*******************************************************************************
 * Example: Sending dfrobot SEN0114 Analog Soil Moisture Sensor data over LoRaWAN,
 *          and controlling a JQC3F-05VDC-C active low relay via downlink message.
 *
 * This sketch reads data from a soil moisture sensor on analog pin A0, splits its 10-bit
 * reading into two bytes, and sends it over LoRaWAN. It also controls a relay connected
 * to digital pin D8 on an Arduino UNO based on downlink commands received from TTN.
 * 
 * Downlink Hex Payload:
 *    0x01 - Turn ON the relay (active low: digital LOW activates the relay)
 *    0x00 - Turn OFF the relay (digital HIGH deactivates the relay)
 *
 * Make sure you replace the placeholder TTN keys with your real keys.
 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#ifdef COMPILE_REGRESSION_TEST
  #define FILLMEIN 0       // Use zero for regression tests.
#else
  #warning "Replace FILLMEIN values with your TTN keys!" 
  #define FILLMEIN (#dont edit this, edit the lines that use FILLMEIN)
#endif

// Application EUI in little-endian format.
static const u1_t PROGMEM APPEUI[8] = { 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
};

void os_getArtEui (u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

// Device EUI in little-endian format.
static const u1_t PROGMEM DEVEUI[8] = { 
  0x67, 0xEB, 0x06, 0xD0, 0x7E, 0xD5, 0xB3, 0x70
};

void os_getDevEui (u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}

// Application Key in big-endian format.
static const u1_t PROGMEM APPKEY[16] = { 
  0x11, 0x7D, 0x72, 0x58, 0xB4, 0x6B, 0xB6, 0x76,
  0x89, 0x38, 0xCC, 0x77, 0x78, 0x21, 0xDA, 0x66
};

void os_getDevKey (u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

// Data array for soil moisture sensor (2 bytes).
static uint8_t mydata[2];

// Job structure for scheduling transmissions.
static osjob_t sendjob;

// Transmission interval in seconds.
const unsigned TX_INTERVAL = 10;

// LMIC pin mapping.
const lmic_pinmap lmic_pins = {
    .nss = 10,                // Chip Select (CS) pin for SPI communication.
    .rxtx = LMIC_UNUSED_PIN,  // RX/TX pin not used.
    .rst = 7,                 // Reset pin for the LoRa module.
    .dio = {2, 5, 6},         // DIO pins for various signals.
};

// Helper function to print two-digit hex.
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
            if (LMIC.dataLen > 0) {
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
              // Process downlink payload to control the relay.
              // The downlink payload is expected to be 1 byte.
              uint8_t cmd = LMIC.frame[LMIC.dataBeg];
              if(cmd == 0x01) {
                 Serial.println(F("Downlink command: Turn ON relay"));
                 digitalWrite(8, LOW);  // Active low relay: LOW turns it ON.
              } else if(cmd == 0x00) {
                 Serial.println(F("Downlink command: Turn OFF relay"));
                 digitalWrite(8, HIGH); // HIGH turns it OFF.
              } else {
                 Serial.println(F("Downlink command: Unknown command"));
              }
            }
            // Schedule the next transmission.
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

void do_send(osjob_t* j) {
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Read the analog value from the soil moisture sensor on A0.
        int sensorValue = analogRead(A0);
        Serial.print(F("Soil Moisture Sensor reading: "));
        Serial.println(sensorValue);
        
        // Split the 10-bit sensor value into two bytes (big-endian).
        mydata[0] = (sensorValue >> 8) & 0xFF;
        mydata[1] = sensorValue & 0xFF;

        // Queue the data for transmission on LoRaWAN port 1 (unconfirmed message).
        LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
        Serial.println(F("Packet queued"));
    }
}

void setup() {
    Serial.begin(9600);
    Serial.println(F("Starting LoRaWAN sensor transmission with relay control"));

    // Initialize the relay control pin (D8) as output.
    pinMode(8, OUTPUT);
    // Set relay off by default (active low relay: HIGH = OFF).
    digitalWrite(8, HIGH);

    os_init();
    LMIC_reset();

    // Start the process: join the network and send sensor data.
    do_send(&sendjob);
}

void loop() {
    os_runloop_once();
}
