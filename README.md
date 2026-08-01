# IRP Irrigation Node

A database mediated multi agent architecture using LLMs for adaptive irrigation on a resource constrained microcontroller. A $10 ESP32-S3 handles only sensing and actuation, while all AI reasoning runs in the cloud through seven coordinated LLM agents. No model and no hardcoded plant logic run on the chip itself.

Undergraduate research project, Department of Electrotechnology, Wayamba University of Sri Lanka. Presented as a poster at WUETecS 2026.

## System Overview

Traditional irrigation controllers use fixed timers or single threshold triggers, treating every plant and soil type the same. This system replaces that with a multi agent LLM pipeline that makes adaptive, species specific, weather aware irrigation decisions.

Seven specialized agents run on n8n: masterAgent, plantReqAgent, weatherForecastAgent, dbAgent, wateringScheduleAgent, gateKeeperAgent, and bridgeAgent. Crop water requirements are computed deterministically using FAO-56 calculations against a custom 322 plant database, live weather data comes from Open Meteo, and every AI generated irrigation proposal must pass a 10 check safety gate before it can be written as a verified command.

The ESP32-S3 never talks to the AI agents directly. It only reads soil moisture, temperature, and humidity, and communicates with the system through five HTTP webhook endpoints exposed by the bridgeAgent, polling for verified commands and posting sensor data and execution logs back. All coordination between the AI layer and the hardware happens through a shared PostgreSQL database.

## How to Use

### Requirements
* VS Code with the PlatformIO extension, or the PlatformIO CLI
* An ESP32-S3 board
* A running backend: PostgreSQL database, n8n instance with the seven agent workflows imported, and the bridgeAgent webhooks reachable from the device

### 1. Configure

Open `include/config.h` and set:

```cpp
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#define SERVER_HOST "http://YOUR_SERVER_HOST:PORT"
#define ZONE_ID 1
#define SOIL_DRY_RAW 3100
#define SOIL_WET_RAW 1200
```

### 2. Build and flash

```bash
pio run
pio run --target upload
pio device monitor --baud 115200
```

### 3. Register a plant

Interact with the Master Orchestrator through the dashboard chat, describing the plant, soil, location, and irrigation method in plain English. The system computes and schedules irrigation automatically from there.

### 4. Monitor

Use the serial console (`status`, `sensors`, `history`) for on device diagnostics, or the web dashboard for live sensor data, command history, and execution logs across all zones.

## Acknowledgements

Developed by E.M.I.S.B. Ekanayake, under the supervision of Mr. T.M.P. Tennakoon.

## License

Academic research project for automated irrigation node control systems.
