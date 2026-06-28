#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <WiFi.h>
#include <ReadyMail.h>
#include "esp_wifi.h"
#include "esp_bt.h"

// --- 1. HARDWARE PIN DEFINITIONS (ESP32-C3) ---
#define I2S_BCLK 4  // Connect to SCK on INMP441
#define I2S_LRC  5  // Connect to WS on INMP441
#define I2S_DOUT 6  // Connect to SD on INMP441

// Changed from 19/20/21 to avoid USB Serial conflicts on the C3
const int red_led    = 7;
const int green_led  = 8;
const int yellow_led = 9;

// --- 2. NETWORK & EMAIL CONFIGURATION ---
const char* ssid = "YOUR_WIFI_NAME"; //1111111111111111111111111111111111111111111111111111111111
const char* password = "YOUR_WIFI_PASSWORD";

const char* smtp_server = "smtp.gmail.com";
const int smtp_port = 465;
const char* email_from = "your_sender_email@gmail.com";
const char* email_pass = "your_app_password";

const char* alert_recipient  = "security_team@example.com";
const char* logger_recipient = "your_personal_email@example.com";
//3333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333
// --- 3. LOGGING & RATE LIMITING STATE ---
unsigned long lastEmailTime = 0;   
unsigned long lastLogTime = 0;     
unsigned long lastHourReset = 0;   
int logsSentThisHour = 0;          
String lastLoggedGrade = "";       

// --- 4. AUDIO CALIBRATION ---
const float CALIBRATION_OFFSET = 50.0;
float smoothed_db = 0.0;
const float ALPHA = 0.05;

// --- FUNCTIONS ---

// Helper: Connects to Wi-Fi, sends email, then turns Wi-Fi OFF
bool sendMail(const char* recipient, String subject, float db, String grade) {
    Serial.println("Turning on Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println();

    bool success = false;
    if (WiFi.status() == WL_CONNECTED) {
        ReadyMail mail;
        mail.setServer(smtp_server, smtp_port);
        mail.setAccount(email_from, email_pass);
        mail.setSubject(subject);
        
        char msg[128];
        sprintf(msg, "Noise Report:\n\nLevel: %.2f dB\nStatus: %s\nUptime: %lu ms", 
                db, grade.c_str(), millis());
        
        mail.setMessage(msg);
        mail.setTo(recipient);

        success = mail.send();
        if (success) Serial.println("Email sent successfully!");
        else Serial.println("Failed to send email.");
    } else {
        Serial.println("Failed to connect to Wi-Fi.");
    }
    
    // Power Optimization: Shut Wi-Fi down immediately after use
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("Wi-Fi powered down.");
    return success;
}

// Helper: Manages quotas, heartbeats, and state changes
void handleEmailLogic(float db, String grade, bool isEmergency) {
    unsigned long currentTime = millis();

    // 1. Reset hour counter every 60 minutes
    if (currentTime - lastHourReset > 3600000) {
        logsSentThisHour = 0;
        lastHourReset = currentTime;
    }

    // 2. URGENT: Security alerts (Prioritized, ignores 3/hr limit)
    if (isEmergency) {
        if (currentTime - lastEmailTime > 300000) { // 30-second anti-spam
            if (sendMail(alert_recipient, "URGENT: Critical Noise Detected!", db, grade)) {
                lastEmailTime = currentTime;
                lastLoggedGrade = grade; 
            }
        }
        return; 
    }

    // 3. LOGS & HEARTBEAT: Limited to 3 per hour
    if (logsSentThisHour < 3) {
        bool isHeartbeat = (currentTime - lastLogTime > 1200000); // 20 minutes
        
        // Trigger if the grade changes OR 20 mins have passed
        if ((grade != lastLoggedGrade) || isHeartbeat) {
            if (sendMail(logger_recipient, "Noise Log Update", db, grade)) {
                lastLoggedGrade = grade;
                lastLogTime = currentTime;
                logsSentThisHour++;
            }
        }
    }
}

// Helper: Calculates decibels from raw I2S buffer
float calculateActualDb(int32_t* buffer, size_t numSamples) {
    float sum_sq = 0;
    for (size_t i = 0; i < numSamples; i++) {
        float sample = (float)buffer[i] / 2147483647.0; // Normalize 32-bit
        sum_sq += sample * sample;
    }
    float rms = sqrt(sum_sq / numSamples);
    if (rms <= 0) return 0; // Prevent log10(0) error
    return 20.0 * log10(rms) + CALIBRATION_OFFSET;
}

// Core Logic: Evaluates sound and triggers LEDs / Emails
void processSensorValue(float db) {
    String grade = "Quiet";
    
    if (db >= 85) {
        grade = "Critical (Red)";
        digitalWrite(red_led, HIGH);
        digitalWrite(yellow_led, LOW);
        digitalWrite(green_led, LOW);
        handleEmailLogic(db, grade, true);
    } else {
        digitalWrite(red_led, LOW);
        if (db >= 70) {
            grade = "Loud (Yellow)";
            digitalWrite(yellow_led, HIGH);
            digitalWrite(green_led, LOW);
        } else if (db >= 40) {
            grade = "Moderate (Green)";
            digitalWrite(yellow_led, LOW);
            digitalWrite(green_led, HIGH);
        } else {
            digitalWrite(yellow_led, LOW);
            digitalWrite(green_led, LOW);
        }
        handleEmailLogic(db, grade, false);
    }
    
    Serial.printf("%.2f dB - %s\n", db, grade.c_str());
}

void setup() {
    // --- 1. POWER OPTIMIZATIONS ---
    setCpuFrequencyMhz(80);         // Cut CPU speed in half
    esp_bt_controller_disable();    // Completely turn off Bluetooth hardware
    WiFi.mode(WIFI_OFF);            // Ensure Wi-Fi starts completely OFF
    
    Serial.begin(115200);
    
    // --- 2. PIN SETUP ---
    pinMode(red_led, OUTPUT);
    pinMode(green_led, OUTPUT);
    pinMode(yellow_led, OUTPUT);

    // --- 3. I2S (INMP441) CONFIGURATION ---
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK, 
        .ws_io_num = I2S_LRC, 
        .data_out_num = -1, 
        .data_in_num = I2S_DOUT
    };
    
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    Serial.println("System Initialized. Listening for noise...");
}

void loop() {
    int32_t buffer[64];
    size_t bytes_read;
    
    // Read audio data from I2S mic
    if (i2s_read(I2S_NUM_0, &buffer, sizeof(buffer), &bytes_read, portMAX_DELAY) == ESP_OK) {
        float raw_db = calculateActualDb(buffer, bytes_read / 4);
        
        // Apply smoothing filter
        smoothed_db = (raw_db * ALPHA) + (smoothed_db * (1.0 - ALPHA));
        
        processSensorValue(smoothed_db);
    }
}