#pragma once

// Central project configuration — edit this file to adapt the firmware to your deployment.

// Wi-Fi Configuration
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// Fast burst retries on first disconnect before handing off to reconnect task
#define WIFI_MAX_RETRY 5

// Delay between background reconnect attempts (ms)
#define WIFI_RECONNECT_DELAY_MS 30000

// Server & Webhooks Configuration (n8n webhook base)
#define SERVER_HOST "http://YOUR_SERVER_HOST:PORT"

#define URL_SENSOR_DATA   SERVER_HOST "/webhook/sensorData"
#define URL_GET_COMMAND   SERVER_HOST "/webhook/getCommands"
#define URL_MARK_EXECUTED SERVER_HOST "/webhook/markExecuted"
#define URL_WATERING_LOG  SERVER_HOST "/webhook/wateringLog"
#define URL_GET_PLANT     SERVER_HOST "/webhook/getPlant"

// HTTP request timeout (ms)
#define HTTP_TIMEOUT_MS 8000

// Retries for mark-executed POST (commands must not be left un-acknowledged)
#define HTTP_MARK_EXEC_RETRIES 3
#define HTTP_RETRY_DELAY_MS    2000

// Zone & Plant Settings
#define ZONE_ID 1

// Plant name buffer size — must hold the longest name in the plants table
#define PLANT_NAME_BUF 64

// How often to refresh the plant name from the server (ms)
#define PLANT_REFRESH_INTERVAL_MS 3600000UL

// GPIO Pins Setup
#define DHT_PIN    4

// Relay IN pin — HIGH = valve open, LOW = valve closed
#define VALVE_GPIO 5

// Built-in WS2812 RGB LED — GPIO 48 on ESP32-S3-DevKitC-1
#define LED_GPIO   48

// ADC Setup (Soil Moisture Sensor) — GPIO14 = ADC1 channel 3 on ESP32-S3
#define SOIL_ADC_PIN 14

// Raw ADC readings for dry / wet calibration (12-bit, 0–4095)
#define SOIL_DRY_RAW 3100
#define SOIL_WET_RAW 1200

// Task Timing Parameters
#define SENSOR_READ_INTERVAL_MS  10000
#define SENSOR_POST_INTERVAL_MS  30000
#define COMMAND_POLL_INTERVAL_MS 15000

// Irrigation Safety Controls
#define VALVE_MAX_DURATION_MIN 60

// OLED Display Setup (SSD1306, 128x64, I2C)
#define OLED_SDA_GPIO 1
#define OLED_SCL_GPIO 2
#define OLED_I2C_ADDR 0x3C
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_SCREEN_INTERVAL_MS 5000
