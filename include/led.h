#pragma once

// led.h — WS2812 RGB status LED driver (Adafruit NeoPixel)

typedef enum {
    LED_BOOTING = 0,     // Yellow pulse  — initialising
    LED_WIFI_CONNECTING, // Orange blink  — connecting
    LED_WIFI_CONNECTED,  // Green heartbeat — online, idle
    LED_WIFI_LOST,       // Red blink     — disconnected
    LED_POLLING,         // Cyan flash    — HTTP request in progress
    LED_IRRIGATING,      // Blue solid    — valve open
    LED_NTP_SYNC,        // Purple flash  — NTP just synced
    LED_ERROR,           // Red solid     — unrecoverable error
} led_state_t;

// Initialise the WS2812 strip and start the LED task. Call once before ledSetState().
void ledInit(void);

// Change the current LED state. Thread-safe.
void ledSetState(led_state_t state);

led_state_t ledGetState(void);

// Returns a human-readable state name (for console display).
const char *ledStateName(led_state_t state);
