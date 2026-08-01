#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "irpHttpClient.h"   // IrrigationCommand type

// irrigation.h — Irrigation command lifecycle task

// Launch the irrigation FreeRTOS task.
// wifi_event_group: task blocks on WIFI_CONNECTED_BIT before each HTTP call.
void irrigationTaskStart(EventGroupHandle_t wifi_event_group, int zoneId);

// Task handle — used by the console abort command.
extern TaskHandle_t irrigationTaskHandle;

// Thread-safe snapshot of the last command fetched from the server.
// cmd->command[0] == '\0' if no command has been polled yet.
void irrigationGetLastCmd(IrrigationCommand *out);
