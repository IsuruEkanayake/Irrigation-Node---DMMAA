#pragma once

// console.h — UART interactive console (Serial0)
//
// Available commands:
//   status          — full system snapshot
//   sensors         — current sensor readings
//   plant           — plant name and zone ID
//   history         — last irrigation event
//   nvs dump        — print all stored NVS keys
//   nvs clear       — erase all NVS data
//   set zone <id>   — change zone ID (reboot to apply)
//   set plant <name>— change plant name
//   led <state>     — force LED state (for testing)
//   abort           — abort an active irrigation
//   reboot          — software reset
//   help            — list commands

// Start the console task. Call once after all subsystems are initialised.
// wifi_events: the application WiFi event group (WIFI_CONNECTED_BIT = BIT0).
void consoleStart(void *wifi_events);
