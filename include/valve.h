#pragma once

#include <stdbool.h>

// valve.h — Solenoid valve / relay GPIO abstraction
//
// VALVE_GPIO in config.h controls the relay.
// HIGH = open (irrigating), LOW = closed (safe default).

// Configure the GPIO and ensure the valve starts CLOSED.
void valveInit(void);

// Open the valve. No-op if already open.
void valveOpen(void);

// Close the valve. Always writes the GPIO even if state appears already closed.
void valveClose(void);

bool valveIsOpen(void);
