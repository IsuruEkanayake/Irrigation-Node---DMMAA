#pragma once

// irpHttpClient.h — HTTP POST/GET helpers
//
// Renamed from httpClient.h to avoid a filename collision with the
// Arduino-ESP32 core's HTTPClient.h on case-insensitive (Windows) filesystems.

#include "sensors.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// One row from the irrigationcommands table.
// command is "IRRIGATE", "SKIP", or "" when no pending row was found.
typedef struct {
    int  commandId;
    char command[16];
    int  durationMinutes;
    int  waterAmountMl;
    char scheduledAt[32];
} IrrigationCommand;

// Payload for inserting one row into the wateringLog table.
typedef struct {
    char plantName[64];
    int  zoneId;
    int  commandId;
    int  durationMinutes;
    int  volumeMl;
    char scheduledAt[32];
    char wateredAt[32];
} WateringLogEntry;

// Must be called once before any HTTP function is used.
void httpClientInit(void);

// POST sensor reading to sensorData webhook. Returns true on HTTP 2xx.
bool httpPostSensorData(const SensorData *data, int zoneId);

// GET the latest unexecuted command for the given zone.
// Returns true and cmd->command != "" when a command exists;
//         true and cmd->command == "" when no pending row;
//         false on network or parse error.
bool httpGetCommand(IrrigationCommand *cmd, int zoneId);

// POST to mark a command as executed. Returns true on HTTP 2xx.
bool httpPostMarkExecuted(int command_id, int zone_id);

// POST completed watering event to the wateringLog webhook. Returns true on HTTP 2xx.
bool httpPostWateringLog(const WateringLogEntry *log);

// GET plant name for the given zone. Returns true and buf filled on success.
bool httpGetPlantName(char *buf, size_t buf_len, int zoneId);