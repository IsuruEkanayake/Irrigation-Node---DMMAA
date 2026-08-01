#pragma once

#include <stdint.h>
#include <stdbool.h>

// sensors.h — DHT11 + soil-moisture ADC driver

typedef struct {
    int      temperature;   // °C
    int      humidity;      // %
    int      soilMoisture;  // 0 = dry, 100 = wet
    uint64_t readingAt_ms;  // millis() at time of read
    bool     valid;
} SensorData;

// Must be called once in setup() before sensorTaskStart().
void sensorInit(void);

// Launch the FreeRTOS sensor task (reads DHT11 + ADC periodically).
void sensorTaskStart(void);

// Thread-safe copy of the latest reading into *out. Returns true if valid.
bool sensorGetLatest(SensorData *out);
