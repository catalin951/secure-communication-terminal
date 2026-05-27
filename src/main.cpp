#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <esp_now.h>
#include <WiFi.h>

// 1. ESP-NOW TARGET CONFIGURATION
// uint8_t peerAddress[] = {0x88, 0xF1, 0x55, 0x31, 0x81, 0x64};
// const char* myName = "BRAVO";

uint8_t peerAddress[] = {0x88, 0xF1, 0x55, 0x32, 0x75, 0xCC};
const char* myName = "ALPHA";

// Secret AES-128 key - 16 bytes!
// Must be identical on both devices
uint8_t secretKey[16] = {0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81, 0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09};

// 2. HARDWARE CONFIGURATION

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// FINGERPRINT
#define FP_RX 16
#define FP_TX 17
HardwareSerial fpSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fpSerial);

// I2S MIC
#define PIN_I2S_WS   GPIO_NUM_33
#define PIN_I2S_SCK  GPIO_NUM_18
#define PIN_I2S_SD   GPIO_NUM_32
#define I2S_PORT_MIC I2S_NUM_0

// I2S SPEAKER
#define PIN_SPK_WS   GPIO_NUM_26
#define PIN_SPK_BCLK GPIO_NUM_27
#define PIN_SPK_DIN  GPIO_NUM_14
#define I2S_PORT_SPK I2S_NUM_1

i2s_chan_handle_t rx_handle = NULL;
i2s_chan_handle_t tx_handle = NULL;

// BUTTONS
#define PIN_BTN_PTT   13
#define PIN_BTN_RESET 19
#define PIN_BTN_ADMIN 23

// 3. SYSTEM AND NETWORK VARIABLES
enum SystemState { STATE_LOCKED, STATE_WAIT_ADMIN, STATE_ADMIN_MENU, STATE_LIVE_STREAM };
SystemState currentState = STATE_LOCKED;

// UI Logic
enum UIMode { UI_NONE, UI_SENDING, UI_RECEIVING, UI_RECEIVED_PACKET };
UIMode currentUI = UI_NONE;
unsigned long lastPacketTime = 0;

// Async Radio Packet Tracker for Locked Notification
volatile unsigned long lastPacketRxTime = 0;

// Admin Logic Variables
unsigned long adminSessionStart = 0;
bool adminBtnPressed = false;
unsigned long adminBtnPressStart = 0;

// Audio packet structure (Max 250 bytes for ESP-NOW)
#define AUDIO_SAMPLES 120 
typedef struct {
    int16_t audioData[AUDIO_SAMPLES];
} audio_packet_t;

audio_packet_t txPacket;
esp_now_peer_info_t peerInfo;

// Queue for received audio
QueueHandle_t audioQueue;

// 4. FUNCTIONS AND CALLBACKS

void setupI2S() {
    // Config Mic (I2S RX)
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT_MIC, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, NULL, &rx_handle);
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000), 
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO), 
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_I2S_SCK, .ws = PIN_I2S_WS, .dout = I2S_GPIO_UNUSED, .din = PIN_I2S_SD, .invert_flags = {0} },
    };
    i2s_channel_init_std_mode(rx_handle, &rx_std_cfg);
    i2s_channel_enable(rx_handle);

    // Config Speaker (I2S TX)
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT_SPK, I2S_ROLE_MASTER);
    i2s_new_channel(&tx_chan_cfg, &tx_handle, NULL);
    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000), 
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), 
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_SPK_BCLK, .ws = PIN_SPK_WS, .dout = PIN_SPK_DIN, .din = I2S_GPIO_UNUSED, .invert_flags = {0} },
    };
    i2s_channel_init_std_mode(tx_handle, &tx_std_cfg);
}

// ESP-NOW Receive Callback
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
    if (len == sizeof(audio_packet_t)) {
        lastPacketRxTime = millis(); // Track incoming traffic timestamp globally
        
        // Only queue audio data if the system is unlocked and live
        if (currentState == STATE_LIVE_STREAM) {
            audio_packet_t rxPacket;
            memcpy(&rxPacket, incomingData, sizeof(rxPacket));
            xQueueSendFromISR(audioQueue, &rxPacket, NULL);
        }
    }
}

// MASTER UI DRAWING FUNCTION
void drawUI(const char* headerTitle, const char* mainText, const char* footerText) {
    display.clearDisplay();
    
    // 1. Header Area
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print(headerTitle); 
    display.print(" | "); 
    display.print(myName);
    display.drawLine(0, 10, 128, 10, WHITE);
    
    // 2. Main Body Area
    display.setTextSize(2);
    int textWidth = strlen(mainText) * 12; 
    int xPos = (128 - textWidth) / 2;
    if (xPos < 0) xPos = 0; 
    display.setCursor(xPos, 25);
    display.print(mainText);
    
    // 3. Footer Area
    display.drawLine(0, 52, 128, 52, WHITE);
    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print(footerText);
    
    display.display();
}

void drawLockedScreen() {
    drawUI("TERMINAL NAME", "LOCKED", "Scan finger to unlock");
}

void drawAdminMenu() {
    drawUI("ADMIN MENU", "SYSTEM OK", "1x: Add | 3s: Delete");
}

bool enrollRoutine(uint8_t id) {
    int p = -1;
    drawUI("ENROLL 1/3", "PUT FINGER", "Scan ID 2");
    
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        if (digitalRead(PIN_BTN_RESET) == LOW) ESP.restart(); 
    }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) return false;
    
    drawUI("ENROLL 2/3", "LIFT", "Wait for scanner...");
    delay(2000);
    p = 0;
    while (p != FINGERPRINT_NOFINGER) {
        p = finger.getImage();
    }
    
    drawUI("ENROLL 3/3", "TAP AGAIN", "Confirming...");
    p = -1;
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        if (digitalRead(PIN_BTN_RESET) == LOW) ESP.restart();
    }
    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) return false;
    
    p = finger.createModel();
    if (p != FINGERPRINT_OK) return false;
    
    p = finger.storeModel(id);
    if (p != FINGERPRINT_OK) return false;
    
    return true;
}

// 5. SETUP
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PIN_BTN_PTT, INPUT_PULLUP);
    pinMode(PIN_BTN_RESET, INPUT_PULLUP);
    pinMode(PIN_BTN_ADMIN, INPUT_PULLUP);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("[DEBUG] OLED Error!");
    drawLockedScreen();

    fpSerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
    audioQueue = xQueueCreate(10, sizeof(audio_packet_t));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_now_init();
    
    esp_now_set_pmk(secretKey);
    esp_now_register_recv_cb(OnDataRecv);
    
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = true; 
    memcpy(peerInfo.lmk, secretKey, 16); 
    
    esp_now_add_peer(&peerInfo);

    setupI2S();
}

// 6. MAIN LOOP
void loop() {
    if (digitalRead(PIN_BTN_RESET) == LOW) {
        ESP.restart();
    }

    switch (currentState) {
        
        case STATE_LOCKED: {
            // Check if there is active incoming radio traffic right now
            bool isIncoming = (millis() - lastPacketRxTime < 500);
            static bool lastIncomingState = false;

            // Update UI dynamically if transmission state changes while locked
            if (isIncoming != lastIncomingState) {
                lastIncomingState = isIncoming;
                if (isIncoming) {
                    drawUI("ALERT", "INCOMING", "Scan finger to listen!");
                    
                    // Generate a quick hardware alert beep tone
                    i2s_channel_enable(tx_handle);
                    int16_t alertTone[AUDIO_SAMPLES];
                    for(int i = 0; i < AUDIO_SAMPLES; i++) {
                        alertTone[i] = (i % 20 < 10) ? 4000 : -4000; // 800Hz square wave
                    }
                    size_t w;
                    // Play 8 short bursts for an audible "ping" notification
                    for(int bursts = 0; bursts < 8; bursts++) {
                        i2s_channel_write(tx_handle, alertTone, sizeof(alertTone), &w, portMAX_DELAY);
                    }
                    i2s_channel_disable(tx_handle); // Disable amp again to save power
                } else {
                    drawLockedScreen();
                }
            }

            // Trigger Admin Login
            if (digitalRead(PIN_BTN_ADMIN) == LOW) {
                drawUI("ADMIN AUTH", "TAP ADMIN", "Place Master Finger");
                currentState = STATE_WAIT_ADMIN;
                adminSessionStart = millis(); 
                delay(300); 
                break;
            }

            // Normal User Login
            uint8_t p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                if (finger.image2Tz() == FINGERPRINT_OK && finger.fingerFastSearch() == FINGERPRINT_OK) {
                    i2s_channel_enable(tx_handle); 
                    currentState = STATE_LIVE_STREAM;
                    currentUI = UI_NONE; 
                }
            }
            break;
        }

        case STATE_WAIT_ADMIN: {
            uint8_t p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                if (finger.image2Tz() == FINGERPRINT_OK && finger.fingerFastSearch() == FINGERPRINT_OK) {
                    if (finger.fingerID == 1) { 
                        currentState = STATE_ADMIN_MENU;
                        adminSessionStart = millis(); 
                        drawAdminMenu();
                    } else {
                        drawUI("SECURITY", "DENIED", "Unauthorized User");
                        delay(2000);
                        currentState = STATE_LOCKED;
                        drawLockedScreen();
                    }
                }
            }
            if (millis() - adminSessionStart > 10000 && currentState != STATE_ADMIN_MENU) {
                currentState = STATE_LOCKED;
                drawLockedScreen();
            }
            break;
        }

        case STATE_ADMIN_MENU: {
            if (millis() - adminSessionStart > 300000) {
                currentState = STATE_LOCKED;
                drawLockedScreen();
                break;
            }

            if (digitalRead(PIN_BTN_ADMIN) == LOW) {
                if (!adminBtnPressed) {
                    adminBtnPressed = true;
                    adminBtnPressStart = millis();
                }
            } else {
                if (adminBtnPressed) {
                    unsigned long pressDuration = millis() - adminBtnPressStart;
                    adminBtnPressed = false;
                    adminSessionStart = millis(); 

                    if (pressDuration >= 3000) { 
                        if (finger.deleteModel(2) == FINGERPRINT_OK) {
                            drawUI("ADMIN MODE", "DELETED", "User removed");
                        } else {
                            drawUI("ADMIN MODE", "ERROR", "Delete failed");
                        }
                        delay(2000);
                        drawAdminMenu();
                    } 
                    else if (pressDuration > 50) { 
                        if (enrollRoutine(2)) { 
                            drawUI("ADMIN MODE", "ADDED", "User enrolled");
                        } else {
                            drawUI("ADMIN MODE", "ERROR", "Enroll failed");
                        }
                        delay(2000);
                        drawAdminMenu();
                    }
                }
            }
            break;
        }

        case STATE_LIVE_STREAM: {
            bool isPTTPressed = (digitalRead(PIN_BTN_PTT) == LOW);
            UIMode targetUI = currentUI; 

            if (isPTTPressed) {
                targetUI = UI_SENDING;
                
                int32_t micBuffer[AUDIO_SAMPLES];
                size_t bytes_read = 0;
                esp_err_t err = i2s_channel_read(rx_handle, micBuffer, sizeof(micBuffer), &bytes_read, portMAX_DELAY);
                
                if (err == ESP_OK && bytes_read > 0) {
                    for (int i = 0; i < AUDIO_SAMPLES; i++) {
                        txPacket.audioData[i] = (int16_t)(micBuffer[i] >> 16);
                    }
                    esp_now_send(peerAddress, (uint8_t *) &txPacket, sizeof(txPacket));
                }
            } 
            else {
                audio_packet_t rxPacket;
                size_t w;
                
                if (xQueueReceive(audioQueue, &rxPacket, 0) == pdTRUE) {
                    targetUI = UI_RECEIVED_PACKET;
                    lastPacketTime = millis();
                    i2s_channel_write(tx_handle, rxPacket.audioData, AUDIO_SAMPLES * sizeof(int16_t), &w, portMAX_DELAY);
                } else {
                    if (millis() - lastPacketTime > 200) {
                        targetUI = UI_RECEIVING;
                    }
                    int16_t silence[AUDIO_SAMPLES] = {0};
                    i2s_channel_write(tx_handle, silence, sizeof(silence), &w, portMAX_DELAY);
                }
            }

            if (targetUI != currentUI) {
                currentUI = targetUI;
                
                if (currentUI == UI_SENDING) {
                    drawUI("TRANSMIT", ">>> TALK", "Streaming audio...");
                } else if (currentUI == UI_RECEIVING) {
                    drawUI("COMMS ONLINE", "READY", "Press PTT to transmit");
                } else if (currentUI == UI_RECEIVED_PACKET) {
                    drawUI("RECEIVE", "<<< VOICE", "Decrypting AES-128...");
                }
            }
            break;
        }
    }
}