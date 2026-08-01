// irrigation.cpp — Irrigation command lifecycle task (FreeRTOS / Arduino-ESP32)
#include <Arduino.h>
#include "irrigation.h"
#include "config.h"
#include "valve.h"
#include "irpHttpClient.h"
#include "led.h"
#include "storage.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <time.h>

#define WIFI_CONNECTED_BIT  BIT0

// Asia/Colombo offset from UTC (UTC+5:30 = 19800 s)
#define COLOMBO_OFFSET_S 19800

// Cache for the active plant name and ID.
static char     sPlantName[PLANT_NAME_BUF] = "Unknown";
static uint64_t sPlantFetchedMs = 0;
static int      sZoneId = 0;
TaskHandle_t    irrigationTaskHandle = NULL;

// Last command polled from server — read by display task via irrigationGetLastCmd()
static IrrigationCommand sLastCmd = {0};
static portMUX_TYPE      sCmdMux  = portMUX_INITIALIZER_UNLOCKED;

// Returns the cached plant name, refreshing from server when stale.
static const char *getPlantName(void)
{
    uint64_t now_ms = (uint64_t)millis();
    uint64_t age_ms = now_ms - sPlantFetchedMs;
    bool stale = (sPlantFetchedMs == 0) ||
                 (age_ms > (uint64_t)PLANT_REFRESH_INTERVAL_MS);

    if (stale) {
        // On first call, seed from NVS before hitting the server
        if (sPlantFetchedMs == 0) {
            char cached[PLANT_NAME_BUF];
            if (storageGetPlantName(cached, sizeof(cached))) {
                strncpy(sPlantName, cached, sizeof(sPlantName) - 1);
                sPlantName[sizeof(sPlantName) - 1] = '\0';
                Serial0.printf("[irrigation] plant loaded from NVS: \"%s\"\n", sPlantName);
            }
        }

        char tmp[PLANT_NAME_BUF];
        ledSetState(LED_POLLING);
        if (httpGetPlantName(tmp, sizeof(tmp), sZoneId)) {
            strncpy(sPlantName, tmp, sizeof(sPlantName) - 1);
            sPlantName[sizeof(sPlantName) - 1] = '\0';
            sPlantFetchedMs = now_ms;
            storageSetPlantName(sPlantName);
            Serial0.printf("[irrigation] plant refreshed: \"%s\"\n", sPlantName);
        } else {
            Serial0.printf("[irrigation] plant refresh failed — using \"%s\"\n", sPlantName);
        }
        ledSetState(LED_WIFI_CONNECTED);
    }
    return sPlantName;
}

// Attempt to mark a command as executed on the server, retrying on failure.
static void markExecutedWithRetry(int command_id, int zone_id)
{
    for (int attempt = 1; attempt <= HTTP_MARK_EXEC_RETRIES; attempt++) {
        if (httpPostMarkExecuted(command_id, zone_id)) {
            return;
        }
        Serial0.printf("[irrigation] markExecuted attempt %d/%d failed for id=%d\n",
                      attempt, HTTP_MARK_EXEC_RETRIES, command_id);
        if (attempt < HTTP_MARK_EXEC_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(HTTP_RETRY_DELAY_MS));
        }
    }
    Serial0.printf("[irrigation] markExecuted FAILED after %d retries for id=%d\n",
                  HTTP_MARK_EXEC_RETRIES, command_id);
}

// Check if the current UTC time has reached or passed the scheduled execution time.
static bool isScheduledTimeReached(const char *scheduledAt)
{
    // No timestamp = run immediately
    if (!scheduledAt || scheduledAt[0] == '\0') return true;

    // Parse "YYYY-MM-DDTHH:MM:SS" (local Colombo time from DB)
    struct tm t = {0};
    int y, mo, d, h, mi, s;
    if (sscanf(scheduledAt, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        Serial0.printf("[irrigation] cannot parse scheduledAt: %s — running now\n",
                       scheduledAt);
        return true;
    }
    t.tm_year  = y - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = d;
    t.tm_hour  = h;
    t.tm_min   = mi;
    t.tm_sec   = s;
    t.tm_isdst = 0;

    // MCU has no TZ set — mktime() treats struct tm as UTC.
    // Subtract Colombo offset to convert local DB time → UTC epoch.
    time_t scheduled_utc = mktime(&t);
    time_t now_utc       = time(NULL);

    Serial0.printf("[irrigation] scheduledAt=%s  sched_utc=%lld  now_utc=%lld  diff=%llds\n",
                   scheduledAt,
                   (long long)scheduled_utc,
                   (long long)now_utc,
                   (long long)(scheduled_utc - now_utc));

    return (now_utc >= scheduled_utc);
}

// Execute an IRRIGATE command: opens valve, monitors abort notifications, and logs outcome.
static void executeIrrigate(const IrrigationCommand *cmd)
{
    int dur_min = cmd->durationMinutes;
    if (dur_min <= 0) {
        Serial0.printf("[irrigation] IRRIGATE with durationMinutes=%d — skipping\n", dur_min);
        return;
    }
    if (dur_min > VALVE_MAX_DURATION_MIN) {
        Serial0.printf("[irrigation] durationMinutes=%d exceeds ceiling %d — clamping\n",
                      dur_min, VALVE_MAX_DURATION_MIN);
        dur_min = VALVE_MAX_DURATION_MIN;
    }

    int planned_vol_ml = cmd->waterAmountMl;

    uint64_t start_ms = (uint64_t)millis();
    valveOpen();
    ledSetState(LED_IRRIGATING);

    Serial0.printf("[irrigation] started — zone=%d  dur=%d min  vol=%d ml\n",
                  sZoneId, dur_min, planned_vol_ml);

    int total_s = dur_min * 60;
    for (int elapsed = 0; elapsed < total_s; elapsed++) {
        uint32_t notifyValue = 0;
        if (xTaskNotifyWait(0, ULONG_MAX, &notifyValue, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (notifyValue == 1) {
                Serial0.println("[irrigation] ABORTED by user command");
                break;
            }
        }
    }

    // Always close the valve
    valveClose();
    ledSetState(LED_WIFI_CONNECTED);

    uint64_t end_ms      = (uint64_t)millis();
    int actual_dur_s     = (int)((end_ms - start_ms) / 1000ULL);
    int actual_dur_min   = (actual_dur_s + 59) / 60;
    int actual_vol_ml    = planned_vol_ml;

    // Build ISO-8601 wateredAt timestamp (UTC)
    char watered_at[32] = "1970-01-01T00:00:00Z";
    time_t now_epoch = time(NULL);
    if (now_epoch > 1000000000) {
        struct tm *utc = gmtime(&now_epoch);
        strftime(watered_at, sizeof(watered_at), "%Y-%m-%dT%H:%M:%SZ", utc);
    }

    Serial0.printf("[irrigation] done — actual=%ds (%dmin)  vol=%dml  at=%s\n",
                  actual_dur_s, actual_dur_min, actual_vol_ml, watered_at);

    storageSaveIrrigation(cmd->commandId, actual_dur_min, actual_vol_ml, watered_at);

    WateringLogEntry log = {};
    log.zoneId          = sZoneId;
    log.commandId       = cmd->commandId;
    log.durationMinutes = actual_dur_min;
    log.volumeMl        = actual_vol_ml;

    strncpy(log.plantName,   getPlantName(),    sizeof(log.plantName)   - 1);
    log.plantName[sizeof(log.plantName) - 1] = '\0';

    strncpy(log.scheduledAt, cmd->scheduledAt,  sizeof(log.scheduledAt) - 1);
    log.scheduledAt[sizeof(log.scheduledAt) - 1] = '\0';

    strncpy(log.wateredAt,   watered_at,        sizeof(log.wateredAt)   - 1);
    log.wateredAt[sizeof(log.wateredAt) - 1] = '\0';

    if (!httpPostWateringLog(&log)) {
        Serial0.println("[irrigation] watering log POST failed — continuing to mark executed");
    }

    markExecutedWithRetry(cmd->commandId, sZoneId);
}

// Execute a SKIP command by marking it executed on the server without opening the valve.
static void executeSkip(const IrrigationCommand *cmd)
{
    Serial0.printf("[irrigation] SKIP zone=%d id=%d — marking executed\n",
                  sZoneId, cmd->commandId);
    markExecutedWithRetry(cmd->commandId, sZoneId);
}

// Main FreeRTOS task loop for polling commands and executing irrigation cycles.
static void irrigationTask(void *arg)
{
    EventGroupHandle_t wifi_events = (EventGroupHandle_t)arg;

    valveInit();

    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    getPlantName();

    uint32_t pollDelay = COMMAND_POLL_INTERVAL_MS;

    while (1) {
        xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);

        IrrigationCommand cmd;
        ledSetState(LED_POLLING);
        bool ok = httpGetCommand(&cmd, sZoneId);
        ledSetState(LED_WIFI_CONNECTED);

        if (ok) {
            taskENTER_CRITICAL(&sCmdMux);
            sLastCmd = cmd;
            taskEXIT_CRITICAL(&sCmdMux);
        }

        if (!ok) {
            Serial0.printf("[irrigation] poll failed — retry in %lu ms\n",
                          (unsigned long)pollDelay);
            vTaskDelay(pdMS_TO_TICKS(pollDelay));
            pollDelay *= 2;
            if (pollDelay > 120000) pollDelay = 120000;
            continue;
        }

        pollDelay = COMMAND_POLL_INTERVAL_MS;

        if (cmd.command[0] == '\0') {
            // No pending command
        } else if (cmd.commandId == storageGetLastCmdId()) {
            // Reboot safety: skip a command we already executed
            Serial0.printf("[irrigation] id=%d already executed — skipping (reboot guard)\n",
                          cmd.commandId);
        } else if (strcmp(cmd.command, "IRRIGATE") == 0) {
            if (isScheduledTimeReached(cmd.scheduledAt)) {
                executeIrrigate(&cmd);
            } else {
                Serial0.printf("[irrigation] id=%d not yet due — will check next poll\n",
                               cmd.commandId);
            }
        } else if (strcmp(cmd.command, "SKIP") == 0) {
            executeSkip(&cmd);
        } else {
            // Unknown command type — log and ignore, do NOT mark as executed
            Serial0.printf("[irrigation] unknown command \"%s\" (id=%d) — ignoring\n",
                           cmd.command, cmd.commandId);
        }

        vTaskDelay(pdMS_TO_TICKS(COMMAND_POLL_INTERVAL_MS));
    }
}

// Start the irrigation FreeRTOS task.
void irrigationTaskStart(EventGroupHandle_t wifi_event_group, int zoneId)
{
    sZoneId = zoneId;
    xTaskCreate(irrigationTask, "irrigationTask", 8192,
                (void *)wifi_event_group, 3, &irrigationTaskHandle);
}

// Retrieve a thread-safe snapshot of the last polled command.
void irrigationGetLastCmd(IrrigationCommand *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&sCmdMux);
    *out = sLastCmd;
    taskEXIT_CRITICAL(&sCmdMux);
}