#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>

// ---------------------------------------------------------
// Heltec V3 Hardware Pin Definitions
// ---------------------------------------------------------
// LoRa SX1262 Pins
#define LORA_CS 8
#define LORA_DIO1 14
#define LORA_RST 12
#define LORA_BUSY 13
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_SCK 9

// OLED Pins
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_PIN 36 // Controls power to the OLED screen

// PRG Button
#define PRG_BUTTON 0

// ---------------------------------------------------------
// Objects & State Variables
// ---------------------------------------------------------
// Initialize the SX1262 radio module
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// Initialize the OLED display (Hardware I2C)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ OLED_RST);

// Interrupt flags
volatile bool receivedFlag = false;
volatile bool enableInterrupt = true;

// Interrupt Service Routine (ISR) triggered when DIO1 goes HIGH
void IRAM_ATTR setFlag(void) {
    if(!enableInterrupt) return;
    receivedFlag = true;
}

// Helper to update the OLED screen
void updateScreen(const char* title, const char* message, float rssi = 0.0) {
    u8g2.clearBuffer();
    
    // Draw Title
    u8g2.setFont(u8g2_font_ncenB08_tr); // Small, bold font
    u8g2.drawStr(0, 10, title);
    u8g2.drawLine(0, 12, 128, 12);
    
    // Draw Message
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(0, 30);
    u8g2.print(message);

    // Draw RSSI if a packet was received
    if (rssi != 0.0) {
        u8g2.setCursor(0, 60);
        u8g2.print("RSSI: ");
        u8g2.print(rssi);
        u8g2.print(" dBm");
    }

    u8g2.sendBuffer();
}

void setup() {
    Serial.begin(115200);
    
    // 1. Power on the OLED (Heltec V3 requires VEXT to be pulled LOW to turn on power)
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);
    delay(50); // Wait for power to stabilize

    // 2. Initialize OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    u8g2.begin();
    updateScreen("Status: Booting...", "Initializing LoRa");

    // 3. Initialize Button
    pinMode(PRG_BUTTON, INPUT_PULLUP);

    // 4. Initialize LoRa SPI bus
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

    // 5. Initialize RadioLib with settings from the Design Doc
    // Frequency: 915.0 MHz, Bandwidth: 125.0 kHz, SF: 9, CR: 4/7, Sync Word: 0x12
    int state = radio.begin(915.0, 125.0, 9, 7, 0x12, 10, 8, 0, false);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("Radio Initialized Successfully!");
        
        // Attach the interrupt to the DIO1 pin
        attachInterrupt(digitalPinToInterrupt(LORA_DIO1), setFlag, RISING);
        
        // Start listening
        radio.startReceive();
        updateScreen("Status: Listening...", "Waiting for msg...");
    } else {
        Serial.print("Radio failed, code ");
        Serial.println(state);
        updateScreen("Status: ERROR", "Radio initialization failed");
        while(true); // Halt execution
    }
}

void loop() {
    // --- STATE 2: RECEIVING ---
    if(receivedFlag) {
        enableInterrupt = false;
        receivedFlag = false;

        String receivedData;
        int state = radio.readData(receivedData);

        if (state == RADIOLIB_ERR_NONE) {
            Serial.println("Message received: " + receivedData);
            float rssi = radio.getRSSI();
            updateScreen("Status: Msg Rcvd!", receivedData.c_str(), rssi);
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            Serial.println("CRC error!");
        }

        // Go back to listening mode
        radio.startReceive();
        enableInterrupt = true;
    }

    // --- STATE 3: TRANSMITTING ---
    if(digitalRead(PRG_BUTTON) == LOW) {
        delay(50); // Simple debounce
        if(digitalRead(PRG_BUTTON) == LOW) {
            updateScreen("Status: Transmitting", "Sending reply...");
            Serial.println("Transmitting reply...");

            // Temporarily disable interrupts while we transmit
            enableInterrupt = false; 
            
            // Send the canned response
            int state = radio.transmit("[Heltec Endpoint]: Message received loud and clear!");
            
            if (state == RADIOLIB_ERR_NONE) {
                updateScreen("Status: Success", "Reply sent!");
            } else {
                updateScreen("Status: Failed", "Tx Error");
            }

            delay(1500); // Leave the success message up for a moment
            
            // Go back to listening
            radio.startReceive();
            enableInterrupt = true;
            updateScreen("Status: Listening...", "Waiting for msg...");

            // Wait until button is released to prevent spamming
            while(digitalRead(PRG_BUTTON) == LOW) { delay(10); } 
        }
    }
}