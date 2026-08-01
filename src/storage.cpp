// storage.cpp — Preferences-based NVS persistence wrapper (Arduino-ESP32)
#include <Arduino.h>
#include "storage.h"
#include "config.h"

#include <Preferences.h>
#include <string.h>

static Preferences  sPrefs;
static const char  *NVS_NS = "mcu";

// Initialize the NVS storage namespace.
void storageInit(void)
{
    sPrefs.begin(NVS_NS, false);
    sPrefs.end();
    Serial0.printf("[storage] NVS namespace \"%s\" ready\n", NVS_NS);
}

// Persist plant name string into NVS storage.
void storageSetPlantName(const char *name)
{
    if (!name) return;
    sPrefs.begin(NVS_NS, false);
    sPrefs.putString("plant_name", name);
    sPrefs.end();
    Serial0.printf("[storage] plant_name saved: \"%s\"\n", name);
}

// Retrieve stored plant name from NVS storage.
bool storageGetPlantName(char *buf, size_t len)
{
    if (!buf || len == 0) return false;
    sPrefs.begin(NVS_NS, true);
    String val = sPrefs.getString("plant_name", "");
    sPrefs.end();
    if (val.length() == 0) return false;
    strncpy(buf, val.c_str(), len - 1);
    buf[len - 1] = '\0';
    return true;
}

// Save zone ID configuration into NVS storage.
void storageSetZoneId(int id)
{
    sPrefs.begin(NVS_NS, false);
    sPrefs.putInt("zone_id", id);
    sPrefs.end();
    Serial0.printf("[storage] zone_id saved: %d\n", id);
}

// Retrieve configured zone ID from NVS storage.
int storageGetZoneId(void)
{
    sPrefs.begin(NVS_NS, true);
    int id = sPrefs.getInt("zone_id", ZONE_ID);
    sPrefs.end();
    return id;
}

// Store last executed irrigation command ID.
void storageSetLastCmdId(int id)
{
    sPrefs.begin(NVS_NS, false);
    sPrefs.putInt("last_cmd_id", id);
    sPrefs.end();
}

// Retrieve last executed irrigation command ID.
int storageGetLastCmdId(void)
{
    sPrefs.begin(NVS_NS, true);
    int id = sPrefs.getInt("last_cmd_id", -1);
    sPrefs.end();
    return id;
}

// Save irrigation session log details to NVS and increment lifetime counter.
void storageSaveIrrigation(int cmd_id, int dur_min, int vol_ml, const char *ts)
{
    sPrefs.begin(NVS_NS, false);
    sPrefs.putInt("last_cmd_id",  cmd_id);
    sPrefs.putInt("last_irr_dur", dur_min);
    sPrefs.putInt("last_irr_vol", vol_ml);
    if (ts) sPrefs.putString("last_irr_ts", ts);

    uint32_t count = sPrefs.getUInt("irr_count", 0);
    count++;
    sPrefs.putUInt("irr_count", count);
    sPrefs.end();

    Serial0.printf("[storage] irrigation saved: cmd=%d dur=%dmin vol=%dml ts=%s count=%lu\n",
                   cmd_id, dur_min, vol_ml, ts ? ts : "?", (unsigned long)count);
}

// Fetch last recorded irrigation session metrics.
bool storageGetLastIrrigation(int *cmd_id, int *dur_min, int *vol_ml,
                               char *ts, size_t ts_len)
{
    sPrefs.begin(NVS_NS, true);

    if (!sPrefs.isKey("last_cmd_id")) {
        sPrefs.end();
        return false;
    }

    int cid = sPrefs.getInt("last_cmd_id",  -1);
    int dur = sPrefs.getInt("last_irr_dur",  0);
    int vol = sPrefs.getInt("last_irr_vol",  0);

    if (ts && ts_len > 0) {
        String tsStr = sPrefs.getString("last_irr_ts", "");
        strncpy(ts, tsStr.c_str(), ts_len - 1);
        ts[ts_len - 1] = '\0';
    }
    sPrefs.end();

    if (cmd_id)  *cmd_id  = cid;
    if (dur_min) *dur_min = dur;
    if (vol_ml)  *vol_ml  = vol;
    return true;
}

// Retrieve cumulative total count of completed irrigation runs.
uint32_t storageGetIrrCount(void)
{
    sPrefs.begin(NVS_NS, true);
    uint32_t count = sPrefs.getUInt("irr_count", 0);
    sPrefs.end();
    return count;
}

// Erase all keys stored in the NVS namespace.
void storageClear(void)
{
    sPrefs.begin(NVS_NS, false);
    bool ok = sPrefs.clear();
    sPrefs.end();
    if (ok) {
        Serial0.printf("[storage] NVS namespace \"%s\" erased\n", NVS_NS);
    } else {
        Serial0.printf("[storage] NVS erase FAILED\n");
    }
}
