// sensors.cpp — DHT11 bit-bang driver and soil-moisture ADC (Arduino-ESP32)
//
// Start pulse uses delay() (not vTaskDelay()) to keep timing tight.
// noInterrupts() is scoped only to the 40-bit read loop.
#include <Arduino.h>
#include "sensors.h"
#include "config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static SensorData        sLatest = {0};
static SemaphoreHandle_t sMutex  = NULL;

// Perform low-level bit-bang timing read from the DHT11 sensor.
static int dhtRead(int *temp, int *hum)
{
    uint8_t data[5] = {0};
    int wait;

    // Start signal: pull low 25 ms (spec min 18 ms), then release
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, LOW);
    delay(25);
    digitalWrite(DHT_PIN, HIGH);
    delayMicroseconds(40);
    pinMode(DHT_PIN, INPUT_PULLUP);
    delayMicroseconds(10);

    // Sensor response handshake
    wait = 0;
    while (digitalRead(DHT_PIN) == HIGH) {
        delayMicroseconds(1);
        if (++wait > 200) {
            Serial0.println("[sensors] DHT11: timeout waiting for response low");
            return -1;
        }
    }

    wait = 0;
    while (digitalRead(DHT_PIN) == LOW) {
        delayMicroseconds(1);
        if (++wait > 200) {
            Serial0.println("[sensors] DHT11: timeout waiting for response high");
            return -1;
        }
    }

    wait = 0;
    while (digitalRead(DHT_PIN) == HIGH) {
        delayMicroseconds(1);
        if (++wait > 200) {
            Serial0.println("[sensors] DHT11: timeout waiting for data start");
            return -1;
        }
    }

    // Read 40 bits: ~50 µs low, then ~26 µs high = 0 or ~70 µs high = 1.
    // Sample 45 µs into the high phase.
    noInterrupts();
    for (int i = 0; i < 40; i++) {
        wait = 0;
        while (digitalRead(DHT_PIN) == LOW) {
            delayMicroseconds(1);
            if (++wait > 200) {
                interrupts();
                Serial0.printf("[sensors] DHT11: timeout on bit %d low\n", i);
                return -1;
            }
        }

        delayMicroseconds(45);
        if (digitalRead(DHT_PIN) == HIGH)
            data[i / 8] |= (1 << (7 - (i % 8)));

        wait = 0;
        while (digitalRead(DHT_PIN) == HIGH) {
            delayMicroseconds(1);
            if (++wait > 200) {
                interrupts();
                Serial0.printf("[sensors] DHT11: timeout on bit %d high\n", i);
                return -1;
            }
        }
    }
    interrupts();

    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if (sum != data[4]) {
        Serial0.printf("[sensors] DHT11: checksum fail — got 0x%02X expected 0x%02X"
                       " (raw: %d %d %d %d %d)\n",
                       sum, data[4],
                       data[0], data[1], data[2], data[3], data[4]);
        return -2;
    }

    *hum  = data[0];
    *temp = data[2];
    return 0;
}

// Read soil moisture ADC raw value and scale to percentage (0-100%).
static int soilReadPct(void)
{
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += analogRead(SOIL_ADC_PIN);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    int avgRaw = sum / 3;
    int pct    = (SOIL_DRY_RAW - avgRaw) * 100 / (SOIL_DRY_RAW - SOIL_WET_RAW);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

// FreeRTOS task loop for reading sensor data at regular intervals.
static void sensorTask(void *arg)
{
    analogReadResolution(12);

    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        int temp = 0, hum = 0;
        int rc = -1;

        for (int attempt = 0; attempt < 3; attempt++) {
            rc = dhtRead(&temp, &hum);
            if (rc == 0) break;
            Serial0.printf("[sensors] DHT11 attempt %d/3 failed (rc=%d)\n",
                           attempt + 1, rc);
            if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(2000));
        }

        SensorData buf   = {0};
        buf.readingAt_ms = (uint64_t)millis();
        buf.soilMoisture = soilReadPct();

        if (rc == 0) {
            buf.temperature = temp;
            buf.humidity    = hum;
            buf.valid       = true;
            Serial0.printf("[sensors] Temp=%d°C  Hum=%d%%  Soil=%d%%\n",
                           temp, hum, buf.soilMoisture);
        } else {
            buf.valid = false;
            Serial0.printf("[sensors] DHT11 failed after 3 attempts — "
                           "soil=%d%%  (retry in %lu ms)\n",
                           buf.soilMoisture,
                           (unsigned long)SENSOR_READ_INTERVAL_MS);
        }

        xSemaphoreTake(sMutex, portMAX_DELAY);
        sLatest = buf;
        xSemaphoreGive(sMutex);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

// Initialize sensor mutex and data structures.
void sensorInit(void)
{
    sMutex = xSemaphoreCreateMutex();
    configASSERT(sMutex != NULL);
}

// Start the FreeRTOS task responsible for polling sensor readings.
void sensorTaskStart(void)
{
    xTaskCreatePinnedToCore(sensorTask, "sensorTask", 6144, NULL, 5, NULL, 1);
}

// Thread-safe fetch of the latest valid sensor readings.
bool sensorGetLatest(SensorData *out)
{
    if (!out) return false;
    xSemaphoreTake(sMutex, portMAX_DELAY);
    *out = sLatest;
    xSemaphoreGive(sMutex);
    return out->valid;
}