// led.cpp — WS2812 RGB status LED driver (Adafruit NeoPixel, FreeRTOS)
//
// Uses std::atomic<int> for cross-task state sharing — correct for .cpp on Arduino-ESP32.
// LED_WIFI_CONNECTED uses PAT_HEARTBEAT: a 100 ms green flash every 3 s,
// indicating idle-online without being distracting.
#include <Arduino.h>
#include "led.h"
#include "config.h"

#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

static Adafruit_NeoPixel sStrip(1, LED_GPIO, NEO_GRB + NEO_KHZ800);

typedef struct { uint8_t r, g, b; } Colour;

static const Colour COLOUR_GREEN  = {  0, 64,  0};
static const Colour COLOUR_YELLOW = { 64, 48,  0};
static const Colour COLOUR_ORANGE = { 80, 24,  0};
static const Colour COLOUR_RED    = { 64,  0,  0};
static const Colour COLOUR_BLUE   = {  0,  0, 64};
static const Colour COLOUR_CYAN   = {  0, 48, 48};
static const Colour COLOUR_PURPLE = { 40,  0, 64};

typedef enum {
    PAT_SOLID,        // constant on
    PAT_BLINK_SLOW,   // 0.5 Hz  — 1 s on / 1 s off
    PAT_BLINK_FAST,   // 4 Hz   — 125 ms on / 125 ms off
    PAT_PULSE,        // smooth fade in/out, ~1 Hz
    PAT_FLASH,        // 100 ms on, 900 ms off
    PAT_TRIPLE_FLASH, // 3 × 80 ms flash then 800 ms off
    PAT_HEARTBEAT,    // 100 ms on then 2900 ms off (total ~3 s)
} Pattern;

typedef struct { Colour colour; Pattern pattern; } StateDesc;

static const StateDesc STATE_TABLE[] = {
    [LED_BOOTING]         = {COLOUR_YELLOW, PAT_PULSE},
    [LED_WIFI_CONNECTING] = {COLOUR_ORANGE, PAT_BLINK_FAST},
    [LED_WIFI_CONNECTED]  = {COLOUR_GREEN,  PAT_HEARTBEAT},
    [LED_WIFI_LOST]       = {COLOUR_RED,    PAT_BLINK_FAST},
    [LED_POLLING]         = {COLOUR_CYAN,   PAT_FLASH},
    [LED_IRRIGATING]      = {COLOUR_BLUE,   PAT_SOLID},
    [LED_NTP_SYNC]        = {COLOUR_PURPLE, PAT_TRIPLE_FLASH},
    [LED_ERROR]           = {COLOUR_RED,    PAT_SOLID},
};

static const char *STATE_NAMES[] = {
    "BOOTING", "WIFI_CONNECTING", "WIFI_CONNECTED", "WIFI_LOST",
    "POLLING", "IRRIGATING",      "NTP_SYNC",       "ERROR",
};

static std::atomic<int> sState{LED_BOOTING};

static void stripSet(Colour c)
{
    sStrip.setPixelColor(0, sStrip.Color(c.r, c.g, c.b));
    sStrip.show();
}

static void stripOff(void)
{
    sStrip.clear();
    sStrip.show();
}

static void renderSolid(Colour c)
{
    stripSet(c);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void renderBlink(Colour c, uint32_t on_ms, uint32_t off_ms)
{
    stripSet(c);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    stripOff();
    vTaskDelay(pdMS_TO_TICKS(off_ms));
}

static void renderPulse(Colour c)
{
    for (int step = 0; step <= 20; step++) {
        float t = step / 20.0f;
        sStrip.setPixelColor(0, sStrip.Color(
            (uint8_t)(c.r * t),
            (uint8_t)(c.g * t),
            (uint8_t)(c.b * t)));
        sStrip.show();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    for (int step = 20; step >= 0; step--) {
        float t = step / 20.0f;
        sStrip.setPixelColor(0, sStrip.Color(
            (uint8_t)(c.r * t),
            (uint8_t)(c.g * t),
            (uint8_t)(c.b * t)));
        sStrip.show();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static void renderFlash(Colour c)
{
    stripSet(c);
    vTaskDelay(pdMS_TO_TICKS(100));
    stripOff();
    vTaskDelay(pdMS_TO_TICKS(900));
}

static void renderTripleFlash(Colour c)
{
    for (int i = 0; i < 3; i++) {
        stripSet(c);
        vTaskDelay(pdMS_TO_TICKS(80));
        stripOff();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    vTaskDelay(pdMS_TO_TICKS(800));
}

// 100 ms tick split into 100 ms slices so state changes are picked up promptly.
static void renderHeartbeat(Colour c)
{
    stripSet(c);
    vTaskDelay(pdMS_TO_TICKS(100));
    stripOff();

    for (int i = 0; i < 29; i++) {
        if (sState.load() != LED_WIFI_CONNECTED) return;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void ledTask(void *arg)
{
    while (1) {
        led_state_t state = (led_state_t)sState.load();
        const StateDesc *d = &STATE_TABLE[state];

        switch (d->pattern) {
        case PAT_SOLID:        renderSolid(d->colour);                break;
        case PAT_BLINK_SLOW:   renderBlink(d->colour, 1000, 1000);    break;
        case PAT_BLINK_FAST:   renderBlink(d->colour, 125, 125);      break;
        case PAT_PULSE:        renderPulse(d->colour);                 break;
        case PAT_FLASH:        renderFlash(d->colour);                 break;
        case PAT_TRIPLE_FLASH: renderTripleFlash(d->colour);           break;
        case PAT_HEARTBEAT:    renderHeartbeat(d->colour);             break;
        default:               vTaskDelay(pdMS_TO_TICKS(100));         break;
        }
    }
}

// Initialise the NeoPixel strip and start the LED blink task.
void ledInit(void)
{
    sStrip.begin();
    sStrip.clear();
    sStrip.show();

    xTaskCreate(ledTask, "ledTask", 2048, NULL, 2, NULL);
    Serial0.printf("[led] init OK (GPIO %d)\n", LED_GPIO);
}

// Change the current LED state (thread-safe).
void ledSetState(led_state_t state)
{
    sState.store((int)state);
}

// Return the current LED state.
led_state_t ledGetState(void)
{
    return (led_state_t)sState.load();
}

// Return a human-readable name for a state (used by the console).
const char *ledStateName(led_state_t state)
{
    if (state < 0 || state >= (int)(sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]))) {
        return "UNKNOWN";
    }
    return STATE_NAMES[state];
}