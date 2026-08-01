#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// storage.h — Preferences-based NVS persistence wrapper
// All keys live under namespace "mcu".

// Must be called once in setup() before any other storage function.
void storageInit(void);

void storageSetPlantName(const char *name);
bool storageGetPlantName(char *buf, size_t len);

// Zone ID — runtime override of config.h ZONE_ID
void storageSetZoneId(int id);
int  storageGetZoneId(void);

void storageSetLastCmdId(int id);
int  storageGetLastCmdId(void);   // returns -1 if never set

void storageSaveIrrigation(int cmd_id, int dur_min, int vol_ml, const char *ts);
bool storageGetLastIrrigation(int *cmd_id, int *dur_min, int *vol_ml,
                               char *ts, size_t ts_len);

uint32_t storageGetIrrCount(void);

void storageClear(void);
