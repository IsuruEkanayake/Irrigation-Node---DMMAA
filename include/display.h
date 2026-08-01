#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// display.h — SSD1306 128x64 OLED display driver and rendering task
//
// Layout:
//   Top bar (8 px):  [WiFi status]   [HH:MM:SS UTC]
//   Separator line   (1 px)
//   Content (55 px): rotates every OLED_SCREEN_INTERVAL_MS between:
//     Screen A — Plant name, temp, humidity, soil bar, valve state
//     Screen B — Next scheduled command, irrigation session count

// Initialise the SSD1306 and start the display task.
// wifi_events: the application WiFi event group (WIFI_CONNECTED_BIT = BIT0).
// Call once from setup() after sensorTaskStart().
void displayInit(EventGroupHandle_t wifi_events);
