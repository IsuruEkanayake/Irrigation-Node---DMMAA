// main.cpp — ESP32-S3 Irrigation Controller entry point (PlatformIO / Arduino-ESP32)
//
// Board  : ESP32-S3 (e.g. "ESP32S3 Dev Module")
// Logs   : Serial0 (UART0 / USB-Serial on ESP32-S3-DevKitC-1)
// Deps   : Adafruit NeoPixel (install via PlatformIO library manager)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "config.h"
#include "console.h"
#include "display.h"
#include "irpHttpClient.h"
#include "irrigation.h"
#include "led.h"
#include "sensors.h"
#include "storage.h"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t sWifiEvents;
static int sRetryCount   = 0;
static int sActiveZoneId = ZONE_ID;

// Wi-Fi event handler: IP acquisition callback.
static void onWifiGotIp(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial0.printf("[main] WiFi connected — IP: %s\n",
                   IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
    sRetryCount = 0;
    xEventGroupClearBits(sWifiEvents, WIFI_FAIL_BIT);
    xEventGroupSetBits(sWifiEvents, WIFI_CONNECTED_BIT);
    ledSetState(LED_WIFI_CONNECTED);
}

// Wi-Fi event handler: Disconnection callback.
static void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
    xEventGroupClearBits(sWifiEvents, WIFI_CONNECTED_BIT);
    ledSetState(LED_WIFI_LOST);

    if (sRetryCount < WIFI_MAX_RETRY) {
        sRetryCount++;
        Serial0.printf("[main] WiFi disconnected — retry %d/%d\n",
                       sRetryCount, WIFI_MAX_RETRY);
        ledSetState(LED_WIFI_CONNECTING);
        WiFi.reconnect();
    } else {
        Serial0.println("[main] WiFi: max retries reached — reconnect task takes over");
        xEventGroupSetBits(sWifiEvents, WIFI_FAIL_BIT);
    }
}

// Initialize Wi-Fi connection and wait for connection event group.
static void wifiInit(void)
{
    sWifiEvents = xEventGroupCreate();
    configASSERT(sWifiEvents != NULL);

    WiFi.onEvent(onWifiGotIp,       ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    ledSetState(LED_WIFI_CONNECTING);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial0.println("[main] Waiting for initial WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(
            sWifiEvents,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        Serial0.println("[main] WiFi connected");
    } else {
        Serial0.println("[main] WiFi connection failed — reconnect task will retry");
        xEventGroupSetBits(sWifiEvents, WIFI_FAIL_BIT);
    }
}

// Synchronize system time with NTP server.
static void sntpInitOnce(void)
{
    configTime(0, 0, "pool.ntp.org");   // UTC, no DST

    Serial0.println("[main] Waiting for NTP sync...");
    unsigned long deadline = millis() + 10000;
    time_t now = 0;
    while (millis() < deadline) {
        time(&now);
        if (now > 1000000000) break;
        delay(200);
    }

    if (now < 1000000000) {
        Serial0.println("[main] NTP sync failed or timed out.");
        return;
    }

    char buf[32];
    struct tm *t = gmtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", t);
    Serial0.printf("[main] NTP time: %s\n", buf);

    ledSetState(LED_NTP_SYNC);
    vTaskDelay(pdMS_TO_TICKS(1500));
    ledSetState(LED_WIFI_CONNECTED);
}

// FreeRTOS background task to attempt Wi-Fi reconnection when connection fails.
static void wifiReconnectTask(void *arg)
{
    while (1) {
        xEventGroupWaitBits(sWifiEvents, WIFI_FAIL_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));

        // Only attempt if still disconnected
        if (xEventGroupGetBits(sWifiEvents) & WIFI_FAIL_BIT) {
            Serial0.println("[main] Reconnect task: attempting WiFi.reconnect()");
            sRetryCount = 0;
            ledSetState(LED_WIFI_CONNECTING);
            WiFi.reconnect();
        }
    }
}

// FreeRTOS task to periodically POST sensor data to server.
static void sensorPostTask(void *arg)
{
    int zoneId = *(int *)arg;
    httpClientInit();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POST_INTERVAL_MS));

        if (!(xEventGroupGetBits(sWifiEvents) & WIFI_CONNECTED_BIT)) {
            Serial0.println("[main] sensorPostTask: WiFi not ready — skipping POST");
            continue;
        }

        SensorData data;
        if (sensorGetLatest(&data)) {
            httpPostSensorData(&data, zoneId);
        } else {
            Serial0.println("[main] sensorPostTask: no valid sensor data yet");
        }
    }
}

// Arduino setup entry point: initializes all hardware peripherals and FreeRTOS tasks.
void setup()
{
    Serial0.begin(115200);
    delay(300);
    Serial0.println("\n[main] IRP Controller booting...");

    storageInit();
    sActiveZoneId = storageGetZoneId();

    ledInit();
    ledSetState(LED_BOOTING);

    wifiInit();
    sntpInitOnce();

    sensorInit();
    sensorTaskStart();

    displayInit(sWifiEvents);

    xTaskCreate(wifiReconnectTask, "wifiReconnect", 2048, NULL, 2, NULL);

    // sensorPostTask receives &sActiveZoneId — file-scope static, pointer stays valid
    xTaskCreate(sensorPostTask, "sensorPost", 8192, &sActiveZoneId, 4, NULL);
    irrigationTaskStart(sWifiEvents, sActiveZoneId);

    consoleStart(sWifiEvents);

    Serial0.println("[main] All tasks started.");
}

// Arduino loop entry point: idle sleep while FreeRTOS tasks execute.
void loop()
{
    // All work is done in FreeRTOS tasks — keep loop() alive without wasting scheduler time
    vTaskDelay(pdMS_TO_TICKS(10000));
}
