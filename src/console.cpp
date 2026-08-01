// console.cpp — UART interactive console (Serial0, FreeRTOS)
#include <Arduino.h>
#include "console.h"
#include "config.h"
#include "led.h"
#include "sensors.h"
#include "storage.h"
#include "irrigation.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define WIFI_CONNECTED_BIT BIT0
#define PROMPT      "mcu> "
#define MAX_CMD_LEN 128
#define MAX_ARGS    8

static EventGroupHandle_t sWifiEvents = NULL;

static void printSeparator(void)
{
    Serial0.println("===========================================");
}

static void utcNowStr(char *buf, size_t len)
{
    time_t now = time(NULL);
    if (now < 1000000000) {
        snprintf(buf, len, "NTP not synced");
        return;
    }
    struct tm *t = gmtime(&now);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", t);
}

// Display full system status overview (Wi-Fi, time, sensors, last irrigation).
static void cmdStatus(void)
{
    char time_str[32];
    utcNowStr(time_str, sizeof(time_str));

    bool wifi_up = sWifiEvents &&
                   (xEventGroupGetBits(sWifiEvents) & WIFI_CONNECTED_BIT);

    char plant[64] = "Unknown";
    storageGetPlantName(plant, sizeof(plant));
    int zone = storageGetZoneId();

    printSeparator();
    Serial0.printf("  ESP32-S3 Irrigation Controller — Zone %d\n", zone);
    printSeparator();
    Serial0.printf("  WiFi        : %s\n", wifi_up ? "CONNECTED" : "DISCONNECTED");
    Serial0.printf("  NTP Time    : %s\n", time_str);
    Serial0.printf("  Plant       : %s\n", plant);
    Serial0.printf("  LED State   : %s\n", ledStateName(ledGetState()));
    Serial0.println();

    int cmd_id, dur, vol;
    char ts[32] = "never";
    bool has_irr = storageGetLastIrrigation(&cmd_id, &dur, &vol, ts, sizeof(ts));
    if (has_irr) {
        Serial0.println("  Last Irrigation:");
        Serial0.printf("    Command ID : %d\n", cmd_id);
        Serial0.printf("    Duration   : %d min\n", dur);
        Serial0.printf("    Volume     : %d ml\n", vol);
        Serial0.printf("    Time       : %s\n", ts);
    } else {
        Serial0.println("  Last Irrigation : none recorded");
    }
    Serial0.println();
    Serial0.printf("  Lifetime irrigations : %lu\n",
                   (unsigned long)storageGetIrrCount());
    Serial0.println();

    SensorData s;
    if (sensorGetLatest(&s) && s.valid) {
        Serial0.println("  Sensor (latest):");
        Serial0.printf("    Temp=%d°C  Hum=%d%%  Soil=%d%%\n",
                       s.temperature, s.humidity, s.soilMoisture);
    } else {
        Serial0.println("  Sensor        : no valid reading yet");
    }
    printSeparator();
}

// Display current sensor values (temperature, humidity, soil moisture).
static void cmdSensors(void)
{
    SensorData s;
    if (!sensorGetLatest(&s) || !s.valid) {
        Serial0.println("No valid sensor data available yet.");
        return;
    }
    Serial0.printf("  Temperature : %d °C\n", s.temperature);
    Serial0.printf("  Humidity    : %d %%\n", s.humidity);
    Serial0.printf("  Soil Moist. : %d %%\n", s.soilMoisture);
}

// Display current plant name and active zone ID.
static void cmdPlant(void)
{
    char plant[64] = "Unknown";
    bool found = storageGetPlantName(plant, sizeof(plant));
    int zone = storageGetZoneId();
    Serial0.printf("  Zone ID    : %d\n", zone);
    Serial0.printf("  Plant Name : %s%s\n", plant,
                   found ? "" : "  (not yet fetched)");
}

// Display summary of last recorded irrigation run.
static void cmdHistory(void)
{
    int cmd_id, dur, vol;
    char ts[32];
    if (!storageGetLastIrrigation(&cmd_id, &dur, &vol, ts, sizeof(ts))) {
        Serial0.println("No irrigation has been logged yet.");
        return;
    }
    Serial0.printf("  Command ID : %d\n", cmd_id);
    Serial0.printf("  Duration   : %d min\n", dur);
    Serial0.printf("  Volume     : %d ml\n", vol);
    Serial0.printf("  Time (UTC) : %s\n", ts);
    Serial0.printf("  Total runs : %lu\n", (unsigned long)storageGetIrrCount());
}

// Manage NVS storage (dump all keys or clear NVS memory).
static void cmdNvs(int argc, char **argv)
{
    if (argc < 2) {
        Serial0.println("Usage: nvs <dump|clear>");
        return;
    }
    if (strcmp(argv[1], "dump") == 0) {
        char plant[64] = "";
        int zone      = storageGetZoneId();
        int last_cmd  = storageGetLastCmdId();
        uint32_t count = storageGetIrrCount();
        int cmd_id, dur, vol;
        char ts[32] = "never";
        storageGetLastIrrigation(&cmd_id, &dur, &vol, ts, sizeof(ts));
        storageGetPlantName(plant, sizeof(plant));

        printSeparator();
        Serial0.println("  NVS Namespace: \"mcu\"");
        printSeparator();
        Serial0.printf("  plant_name   : \"%s\"\n", plant[0] ? plant : "<not set>");
        Serial0.printf("  zone_id      : %d\n", zone);
        Serial0.printf("  last_cmd_id  : %d\n", last_cmd);
        Serial0.printf("  last_irr_dur : %d min\n", dur);
        Serial0.printf("  last_irr_vol : %d ml\n", vol);
        Serial0.printf("  last_irr_ts  : %s\n", ts);
        Serial0.printf("  irr_count    : %lu\n", (unsigned long)count);
        printSeparator();

    } else if (strcmp(argv[1], "clear") == 0) {
        Serial0.print("Type 'yes' to erase all NVS data: ");
        unsigned long deadline = millis() + 10000;
        String confirm = "";
        while (millis() < deadline) {
            if (Serial0.available()) {
                confirm = Serial0.readStringUntil('\n');
                confirm.trim();
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (confirm == "yes") {
            storageClear();
            Serial0.println("NVS cleared.");
        } else {
            Serial0.println("Cancelled.");
        }
    } else {
        Serial0.printf("Unknown nvs subcommand: %s\n", argv[1]);
    }
}

// Update runtime configuration variables (zone ID or plant name).
static void cmdSet(int argc, char **argv)
{
    if (argc < 3) {
        Serial0.println("Usage: set zone <id> | set plant <name>");
        return;
    }
    if (strcmp(argv[1], "zone") == 0) {
        int id = atoi(argv[2]);
        if (id <= 0) { Serial0.println("Invalid zone ID."); return; }
        storageSetZoneId(id);
        Serial0.printf("Zone ID set to %d (takes effect after reboot).\n", id);
    } else if (strcmp(argv[1], "plant") == 0) {
        storageSetPlantName(argv[2]);
        Serial0.printf("Plant name set to \"%s\".\n", argv[2]);
    } else {
        Serial0.printf("Unknown field: %s\n", argv[1]);
    }
}

// Manually override RGB status LED state for testing.
static void cmdLed(int argc, char **argv)
{
    if (argc < 2) {
        Serial0.println("Usage: led <booting|connecting|connected|lost|polling|irrigating|ntp|error>");
        return;
    }
    struct { const char *name; led_state_t state; } map[] = {
        {"booting",    LED_BOOTING},
        {"connecting", LED_WIFI_CONNECTING},
        {"connected",  LED_WIFI_CONNECTED},
        {"lost",       LED_WIFI_LOST},
        {"polling",    LED_POLLING},
        {"irrigating", LED_IRRIGATING},
        {"ntp",        LED_NTP_SYNC},
        {"error",      LED_ERROR},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(argv[1], map[i].name) == 0) {
            ledSetState(map[i].state);
            Serial0.printf("LED set to %s\n", ledStateName(map[i].state));
            return;
        }
    }
    Serial0.printf("Unknown state: %s\n", argv[1]);
}

// Software reset the microcontroller.
static void cmdReboot(void)
{
    Serial0.println("Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP.restart();
}

// Send abort signal to active irrigation task loop.
static void cmdAbort(void)
{
    if (irrigationTaskHandle != NULL) {
        xTaskNotify(irrigationTaskHandle, 1, eSetValueWithOverwrite);
        Serial0.println("Abort signal sent to irrigation task.");
    } else {
        Serial0.println("Irrigation task handle is NULL.");
    }
}

// Print available console commands and usage notes.
static void cmdHelp(void)
{
    Serial0.println("Available commands:");
    Serial0.println("  status              — Full system snapshot");
    Serial0.println("  sensors             — Current sensor readings");
    Serial0.println("  plant               — Plant name and zone ID");
    Serial0.println("  history             — Last irrigation event");
    Serial0.println("  nvs dump            — Print all stored NVS keys");
    Serial0.println("  nvs clear           — Erase all NVS data");
    Serial0.println("  set zone <id>       — Change zone ID (reboot to apply)");
    Serial0.println("  set plant <name>    — Change plant name");
    Serial0.println("  led <state>         — Force LED state (for testing)");
    Serial0.println("  abort               — Abort active irrigation");
    Serial0.println("  reboot              — Software reset");
    Serial0.println("  help                — Show this list");
}

// Split input string by space/tab delimiters into argument array.
static int tokenise(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

// Dispatch parsed command to corresponding handler function.
static void dispatch(int argc, char **argv)
{
    if (argc == 0) return;
    const char *cmd = argv[0];

    if      (strcmp(cmd, "status")  == 0) { cmdStatus();             }
    else if (strcmp(cmd, "sensors") == 0) { cmdSensors();            }
    else if (strcmp(cmd, "plant")   == 0) { cmdPlant();              }
    else if (strcmp(cmd, "history") == 0) { cmdHistory();            }
    else if (strcmp(cmd, "nvs")     == 0) { cmdNvs(argc, argv);      }
    else if (strcmp(cmd, "set")     == 0) { cmdSet(argc, argv);      }
    else if (strcmp(cmd, "led")     == 0) { cmdLed(argc, argv);      }
    else if (strcmp(cmd, "reboot")  == 0) { cmdReboot();             }
    else if (strcmp(cmd, "abort")   == 0) { cmdAbort();              }
    else if (strcmp(cmd, "help")    == 0) { cmdHelp();               }
    else {
        Serial0.printf("Unknown command: '%s' — type 'help'\n", cmd);
    }
}

// FreeRTOS task loop implementing interactive UART command prompt.
static void consoleTask(void *arg)
{
    (void)arg;
    static char lineBuf[MAX_CMD_LEN];
    int lineLen = 0;

    Serial0.print(PROMPT);

    while (1) {
        if (!Serial0.available()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        char c = (char)Serial0.read();
        Serial0.print(c);   // echo

        if (c == '\r') c = '\n';

        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            if (lineLen > 0) {
                char *argv[MAX_ARGS];
                int argc = tokenise(lineBuf, argv, MAX_ARGS);
                dispatch(argc, argv);
            }
            lineLen = 0;
            Serial0.print(PROMPT);

        } else if (c == '\b' || c == 127) {
            if (lineLen > 0) {
                lineLen--;
                Serial0.print("\b \b");
            }
        } else if (lineLen < MAX_CMD_LEN - 1) {
            lineBuf[lineLen++] = c;
        }
    }
}

// Start the interactive console task on Serial0.
void consoleStart(void *wifi_events)
{
    sWifiEvents = (EventGroupHandle_t)wifi_events;
    xTaskCreate(consoleTask, "consoleTask", 6144, NULL, 3, NULL);
    Serial0.println("[console] started — type 'help' for commands");
}
