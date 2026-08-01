// irpHttpClient.cpp — HTTP POST/GET helpers for the irrigation node.
//
// Renamed from httpClient.cpp to avoid a filename collision with the
// Arduino-ESP32 core's HTTPClient.h on case-insensitive (Windows) filesystems.
#include <Arduino.h>
#include "irpHttpClient.h"
#include "config.h"

#include <HTTPClient.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Find an integer field in a flat JSON string. Returns false if not found.
static bool jsonGetInt(const char *json, const char *key, int *out)
{
    char token[48];
    snprintf(token, sizeof(token), "\"%s\":", key);

    const char *p = strstr(json, token);
    if (!p) return false;
    p += strlen(token);

    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    return (sscanf(p, "%d", out) == 1);
}

// Find a string field in a flat JSON string. Returns false if not found or empty.
static bool jsonGetStr(const char *json, const char *key,
                       char *out, size_t out_len)
{
    char token[48];
    snprintf(token, sizeof(token), "\"%s\":", key);

    const char *p = strstr(json, token);
    if (!p) return false;
    p += strlen(token);

    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p != '"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (i > 0);
}

#define RESP_BUF_SIZE 512

// Send a JSON POST to url, optionally capture the response body. Returns HTTP status or -1.
static int doPost(const char *url, const char *body,
                  char *responseBody, size_t responseBodyLen)
{
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    int status = http.POST((uint8_t *)body, (int)strlen(body));

    if (status < 0) {
        Serial0.printf("[httpClient] POST %s failed: %s\n",
                       url, http.errorToString(status).c_str());
        http.end();
        return -1;
    }

    if (responseBody && responseBodyLen > 0) {
        String resp = http.getString();
        strncpy(responseBody, resp.c_str(), responseBodyLen - 1);
        responseBody[responseBodyLen - 1] = '\0';
    }

    http.end();
    return status;
}

// Send a GET to url, optionally capture the response body. Returns HTTP status or -1.
static int doGet(const char *url, char *responseBody, size_t responseBodyLen)
{
    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int status = http.GET();

    if (status < 0) {
        Serial0.printf("[httpClient] GET %s failed: %s\n",
                       url, http.errorToString(status).c_str());
        http.end();
        return -1;
    }

    if (responseBody && responseBodyLen > 0) {
        String resp = http.getString();
        strncpy(responseBody, resp.c_str(), responseBodyLen - 1);
        responseBody[responseBodyLen - 1] = '\0';
    }

    http.end();
    return status;
}

// No-op — each request creates its own HTTPClient stack handle.
void httpClientInit(void)
{
    // No global state — each request builds its own HTTPClient handle
}

// POST sensor readings for zoneId. Returns true on HTTP 2xx.
bool httpPostSensorData(const SensorData *data, int zoneId)
{
    if (!data || !data->valid) {
        Serial0.println("[httpClient] httpPostSensorData: no valid sensor data");
        return false;
    }

    char body[160];
    snprintf(body, sizeof(body),
             "{\"zoneId\":%d,"
             "\"soilMoisturePct\":%d,"
             "\"temperatureC\":%d,"
             "\"humidityPct\":%d}",
             zoneId,
             data->soilMoisture,
             data->temperature,
             data->humidity);

    Serial0.printf("[httpClient] POST sensorData  body: %s\n", body);

    char resp[RESP_BUF_SIZE] = {0};
    int status = doPost(URL_SENSOR_DATA, body, resp, sizeof(resp));

    bool ok = (status >= 200 && status < 300);
    Serial0.printf("[httpClient] sensorData %s status=%d\n",
                   ok ? "OK" : "FAIL", status);
    return ok;
}

// GET the pending command for zoneId. Returns true on success, false on network error.
bool httpGetCommand(IrrigationCommand *cmd, int zoneId)
{
    if (!cmd) return false;
    memset(cmd, 0, sizeof(*cmd));

    char url[128];
    snprintf(url, sizeof(url), "%s?zoneId=%d", URL_GET_COMMAND, zoneId);

    Serial0.printf("[httpClient] GET getCommands  url: %s\n", url);

    char resp[RESP_BUF_SIZE] = {0};
    int status = doGet(url, resp, sizeof(resp));

    if (status < 0) {
        Serial0.println("[httpClient] transport error");
        return false;
    }
    Serial0.printf("[httpClient] status=%d  resp: %s\n",
                   status, resp[0] ? resp : "<empty>");

    // 204 No Content = no pending command
    if (status == 204 || resp[0] == '\0') {
        return true;
    }
    if (status < 200 || status >= 300) {
        Serial0.printf("[httpClient] HTTP %d\n", status);
        return false;
    }

    if (!jsonGetInt(resp, "commandId", &cmd->commandId)) {
        return true;  // no commandId = no pending command
    }
    jsonGetStr(resp, "command",         cmd->command,         sizeof(cmd->command));
    jsonGetInt(resp, "durationMinutes", &cmd->durationMinutes);
    jsonGetInt(resp, "waterAmountMl",   &cmd->waterAmountMl);
    jsonGetStr(resp, "scheduledAt",     cmd->scheduledAt,     sizeof(cmd->scheduledAt));

    Serial0.printf("[httpClient] command: id=%d  cmd=%s  dur=%d min  vol=%d ml\n",
                   cmd->commandId, cmd->command,
                   cmd->durationMinutes, cmd->waterAmountMl);
    return true;
}

// POST to mark a command as executed. Retries are handled by the caller. Returns true on HTTP 2xx.
bool httpPostMarkExecuted(int command_id, int zone_id)
{
    char body[64];
    snprintf(body, sizeof(body),
             "{\"commandId\":%d,\"zoneId\":%d}",
             command_id, zone_id);

    Serial0.printf("[httpClient] POST markExecuted  body: %s\n", body);

    char resp[RESP_BUF_SIZE] = {0};
    int status = doPost(URL_MARK_EXECUTED, body, resp, sizeof(resp));

    bool ok = (status >= 200 && status < 300);
    if (ok) {
        Serial0.printf("[httpClient] markExecuted OK: id=%d zone=%d\n",
                       command_id, zone_id);
    } else {
        Serial0.printf("[httpClient] markExecuted FAIL: status=%d id=%d\n",
                       status, command_id);
    }
    return ok;
}

// POST a completed watering event to the wateringLog webhook. Returns true on HTTP 2xx.
bool httpPostWateringLog(const WateringLogEntry *log)
{
    if (!log) return false;

    char body[320];
    snprintf(body, sizeof(body),
             "{\"zoneId\":%d,"
             "\"plantName\":\"%s\","
             "\"commandId\":%d,"
             "\"durationMinutes\":%d,"
             "\"volumeMl\":%d,"
             "\"scheduledAt\":\"%s\","
             "\"wateredAt\":\"%s\"}",
             log->zoneId,
             log->plantName,
             log->commandId,
             log->durationMinutes,
             log->volumeMl,
             log->scheduledAt,
             log->wateredAt);

    Serial0.printf("[httpClient] POST wateringLog  body: %s\n", body);

    char resp[RESP_BUF_SIZE] = {0};
    int status = doPost(URL_WATERING_LOG, body, resp, sizeof(resp));

    bool ok = (status >= 200 && status < 300);
    Serial0.printf("[httpClient] wateringLog %s status=%d\n",
                   ok ? "OK" : "FAIL", status);
    return ok;
}

// GET the plant name for zoneId and copy it into buf. Returns true on success.
bool httpGetPlantName(char *buf, size_t buf_len, int zoneId)
{
    if (!buf || buf_len == 0) return false;

    char url[128];
    snprintf(url, sizeof(url), "%s?zoneId=%d", URL_GET_PLANT, zoneId);

    Serial0.printf("[httpClient] GET getPlant  url: %s\n", url);

    char resp[RESP_BUF_SIZE] = {0};
    int status = doGet(url, resp, sizeof(resp));

    if (status < 200 || status >= 300 || resp[0] == '\0') {
        Serial0.printf("[httpClient] getPlant FAIL status=%d\n", status);
        return false;
    }

    if (!jsonGetStr(resp, "plantName", buf, buf_len)) {
        Serial0.printf("[httpClient] could not parse plantName from: %s\n", resp);
        return false;
    }

    Serial0.printf("[httpClient] plantName=%s (zone %d)\n", buf, zoneId);
    return true;
}