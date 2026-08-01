// valve.cpp — Solenoid valve / relay GPIO abstraction (Arduino-ESP32)
#include <Arduino.h>
#include "valve.h"
#include "config.h"

// Single source of truth for valve state — never read back the GPIO
static bool sOpen = false;

void valveInit(void)
{
    pinMode(VALVE_GPIO, OUTPUT);
    digitalWrite(VALVE_GPIO, LOW);
    sOpen = false;
    Serial0.printf("[valve] init — GPIO %d, state=CLOSED\n", VALVE_GPIO);
}

void valveOpen(void)
{
    if (sOpen) return;
    digitalWrite(VALVE_GPIO, HIGH);
    sOpen = true;
    Serial0.println("[valve] OPEN");
}

void valveClose(void)
{
    // Always write LOW — safe recovery even if sOpen is stale
    digitalWrite(VALVE_GPIO, LOW);
    sOpen = false;
    Serial0.println("[valve] CLOSED");
}

bool valveIsOpen(void)
{
    return sOpen;
}
