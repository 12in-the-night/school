#include <Arduino.h>
#include "Audio.h"            // Ensure you have the 'ESP32-audioI2S' library installed
#include <driver/i2s.h>       // Standard ESP32 I2S driver
#include <math.h>             // Needed for log10 and sqrt calculations

// --- 1. HARDWARE PIN DEFINITIONS ---
// Update these to match your specific wiring
#define I2S_BCLK 27
#define I2S_LRC  25
#define I2S_DOUT 33

// LED Pin Assignments
const int red_led    = 19;
const int green_led  = 20;
const int yellow_led = 21;

// --- 2. CALIBRATION & LOGIC ---
// Increase this offset if your readings are too low compared to a real SPL meter
const float CALIBRATION_OFFSET = 50.0; 

// --- GLOBAL OBJECTS ---
Audio audio; 

// Function prototypes
float calculateActualDb(int32_t* buffer, size_t numSamples);
void processSensorValue(float db_meter);

void setup() {
    Serial.begin(115200);

    // Initialize LED pins
    pinMode(red_led, OUTPUT);
    pinMode(green_led, OUTPUT);
    pinMode(yellow_led, OUTPUT);

    // Configure I2S Hardware
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

    // Install and start I2S driver
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    // Audio library initialization
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(10);
    
    Serial.println("System Initialized. Starting Sound Detection...");
}

void loop() {
    audio.loop(); // Process audio data