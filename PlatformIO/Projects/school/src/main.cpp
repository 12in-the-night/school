#include <Arduino.h>
#include "Audio.h"
#include <driver/i2s.h>
#include <math.h>

// --- 1. HARDWARE PIN DEFINITIONS ---
#define I2S_BCLK 27
#define I2S_LRC  25
#define I2S_DOUT 33

const int red_led    = 19;
const int green_led  = 20;
const int yellow_led = 21;

// --- 2. CALIBRATION & LOGIC ---
const float CALIBRATION_OFFSET = 50.0;
float smoothed_db = 0.0;
const float ALPHA = 0.05; // Smoothing factor (0.01 - 0.1)

Audio audio;

// Helper to calculate dB from I2S buffer
float calculateActualDb(int32_t* buffer, size_t numSamples) {
    float sum_sq = 0;
    for (size_t i = 0; i < numSamples; i++) {
        float sample = (float)buffer[i] / 2147483647.0; // Normalize 32-bit to -1.0 to 1.0
        sum_sq += sample * sample;
    }
    float rms = sqrt(sum_sq / numSamples);
    return 20.0 * log10(rms) + CALIBRATION_OFFSET;
}

// Map dB to LEDs
void processSensorValue(float db) {
    // Example thresholds: adjust as needed for your environment
    digitalWrite(green_led,  (db > 40 && db < 70) ? HIGH : LOW);
    digitalWrite(yellow_led, (db >= 70 && db < 85) ? HIGH : LOW);
    digitalWrite(red_led,    (db >= 85) ? HIGH : LOW);
    
    Serial.printf("dB: %.2f\n", db);
}

void setup() {
    Serial.begin(115200);

    pinMode(red_led, OUTPUT);
    pinMode(green_led, OUTPUT);
    pinMode(yellow_led, OUTPUT);

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

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(10);
    
    Serial.println("System Initialized.");
}

void loop() {
    audio.loop();

    int32_t buffer[64];
    size_t bytes_read;
    
    // Read I2S data
    if (i2s_read(I2S_NUM_0, &buffer, sizeof(buffer), &bytes_read, portMAX_DELAY) == ESP_OK) {
        float raw_db = calculateActualDb(buffer, bytes_read / 4);
        
        // Apply EMA filter
        smoothed_db = (raw_db * ALPHA) + (smoothed_db * (1.0 - ALPHA));
        
        processSensorValue(smoothed_db);
    }
}