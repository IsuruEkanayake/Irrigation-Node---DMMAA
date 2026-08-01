// display.cpp — SSD1306 128x64 OLED driver and rendering task (Arduino-ESP32, I2C)
#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "sensors.h"
#include "storage.h"
#include "valve.h"
#include "irrigation.h"
#include "irpHttpClient.h"

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define WIFI_CONNECTED_BIT BIT0

#define COLS  128
#define ROWS   64
#define PAGES   8   // 64 px / 8 bits per page

// Framebuffer: [page][col], bit0 = topmost pixel of each page row
static uint8_t sFb[PAGES][COLS];

// Send single command byte to SSD1306 via I2C.
static void oledCmd(uint8_t c)
{
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x00);   // Co=0, D/C=0 -> command
    Wire.write(c);
    Wire.endTransmission();
}

static void oledData(const uint8_t *buf, size_t n)
{
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x40);   // Co=0, D/C=1 -> data
    if (n > COLS) n = COLS;
    Wire.write(buf, n);
    Wire.endTransmission();
}

static void oledHwInit(void)
{
    static const uint8_t seq[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock divider / oscillator
        0xA8, 0x3F,  // multiplex ratio (64 rows)
        0xD3, 0x00,  // display offset = 0
        0x40,        // start line = 0
        0x8D, 0x14,  // charge pump enable
        0x20, 0x00,  // horizontal addressing mode
        0xA1,        // segment remap (col 127 -> SEG0)
        0xC8,        // COM scan remapped
        0xDA, 0x12,  // COM pins config (128x64)
        0x81, 0xCF,  // contrast
        0xD9, 0xF1,  // pre-charge period
        0xDB, 0x40,  // VCOMH deselect level
        0xA4,        // output follows RAM
        0xA6,        // normal display (not inverted)
        0xAF,        // display on
    };
    for (size_t i = 0; i < sizeof(seq); i++) oledCmd(seq[i]);
}

// Flush internal framebuffer array to SSD1306 RAM over I2C.
static void fbFlush(void)
{
    oledCmd(0x21); oledCmd(0x00); oledCmd(0x7F);  // col  0-127
    oledCmd(0x22); oledCmd(0x00); oledCmd(0x07);  // page 0-7
    for (int p = 0; p < PAGES; p++) oledData(sFb[p], COLS);
}

static void fbClear(void)
{
    memset(sFb, 0, sizeof(sFb));
}

static void fbClearRegion(int pageStart, int pageEnd)
{
    for (int p = pageStart; p <= pageEnd && p < PAGES; p++)
        memset(sFb[p], 0, COLS);
}

static void fbPixel(int x, int y, bool on)
{
    if ((unsigned)x >= COLS || (unsigned)y >= ROWS) return;
    uint8_t mask = 1u << (y & 7);
    if (on) sFb[y >> 3][x] |=  mask;
    else    sFb[y >> 3][x] &= ~mask;
}

// 6x8 Font lookup table — ASCII 0x20–0x7E (5 columns + 1 spacer per glyph).
static const uint8_t kFont[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 20 space
    {0x00,0x00,0x5F,0x00,0x00}, // 21 !
    {0x00,0x07,0x00,0x07,0x00}, // 22 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 23 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 24 $
    {0x23,0x13,0x08,0x64,0x62}, // 25 %
    {0x36,0x49,0x55,0x22,0x50}, // 26 &
    {0x00,0x05,0x03,0x00,0x00}, // 27 '
    {0x00,0x1C,0x22,0x41,0x00}, // 28 (
    {0x00,0x41,0x22,0x1C,0x00}, // 29 )
    {0x14,0x08,0x3E,0x08,0x14}, // 2A *
    {0x08,0x08,0x3E,0x08,0x08}, // 2B +
    {0x00,0x50,0x30,0x00,0x00}, // 2C ,
    {0x08,0x08,0x08,0x08,0x08}, // 2D -
    {0x00,0x60,0x60,0x00,0x00}, // 2E .
    {0x20,0x10,0x08,0x04,0x02}, // 2F /
    {0x3E,0x51,0x49,0x45,0x3E}, // 30 0
    {0x00,0x42,0x7F,0x40,0x00}, // 31 1
    {0x42,0x61,0x51,0x49,0x46}, // 32 2
    {0x21,0x41,0x45,0x4B,0x31}, // 33 3
    {0x18,0x14,0x12,0x7F,0x10}, // 34 4
    {0x27,0x45,0x45,0x45,0x39}, // 35 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 36 6
    {0x01,0x71,0x09,0x05,0x03}, // 37 7
    {0x36,0x49,0x49,0x49,0x36}, // 38 8
    {0x06,0x49,0x49,0x29,0x1E}, // 39 9
    {0x00,0x36,0x36,0x00,0x00}, // 3A :
    {0x00,0x56,0x36,0x00,0x00}, // 3B ;
    {0x08,0x14,0x22,0x41,0x00}, // 3C <
    {0x14,0x14,0x14,0x14,0x14}, // 3D =
    {0x00,0x41,0x22,0x14,0x08}, // 3E >
    {0x02,0x01,0x51,0x09,0x06}, // 3F ?
    {0x32,0x49,0x79,0x41,0x3E}, // 40 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 41 A
    {0x7F,0x49,0x49,0x49,0x36}, // 42 B
    {0x3E,0x41,0x41,0x41,0x22}, // 43 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 44 D
    {0x7F,0x49,0x49,0x49,0x41}, // 45 E
    {0x7F,0x09,0x09,0x09,0x01}, // 46 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 47 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 48 H
    {0x00,0x41,0x7F,0x41,0x00}, // 49 I
    {0x20,0x40,0x41,0x3F,0x01}, // 4A J
    {0x7F,0x08,0x14,0x22,0x41}, // 4B K
    {0x7F,0x40,0x40,0x40,0x40}, // 4C L
    {0x7F,0x02,0x0C,0x02,0x7F}, // 4D M
    {0x7F,0x04,0x08,0x10,0x7F}, // 4E N
    {0x3E,0x41,0x41,0x41,0x3E}, // 4F O
    {0x7F,0x09,0x09,0x09,0x06}, // 50 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 51 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 52 R
    {0x46,0x49,0x49,0x49,0x31}, // 53 S
    {0x01,0x01,0x7F,0x01,0x01}, // 54 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 55 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 56 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 57 W
    {0x63,0x14,0x08,0x14,0x63}, // 58 X
    {0x07,0x08,0x70,0x08,0x07}, // 59 Y
    {0x61,0x51,0x49,0x45,0x43}, // 5A Z
    {0x00,0x7F,0x41,0x41,0x00}, // 5B [
    {0x02,0x04,0x08,0x10,0x20}, // 5C backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 5D ]
    {0x04,0x02,0x01,0x02,0x04}, // 5E ^
    {0x40,0x40,0x40,0x40,0x40}, // 5F _
    {0x00,0x01,0x02,0x04,0x00}, // 60 `
    {0x20,0x54,0x54,0x54,0x78}, // 61 a
    {0x7F,0x48,0x44,0x44,0x38}, // 62 b
    {0x38,0x44,0x44,0x44,0x20}, // 63 c
    {0x38,0x44,0x44,0x48,0x7F}, // 64 d
    {0x38,0x54,0x54,0x54,0x18}, // 65 e
    {0x08,0x7E,0x09,0x01,0x02}, // 66 f
    {0x0C,0x52,0x52,0x52,0x3E}, // 67 g
    {0x7F,0x08,0x04,0x04,0x78}, // 68 h
    {0x00,0x44,0x7D,0x40,0x00}, // 69 i
    {0x20,0x40,0x44,0x3D,0x00}, // 6A j
    {0x7F,0x10,0x28,0x44,0x00}, // 6B k
    {0x00,0x41,0x7F,0x40,0x00}, // 6C l
    {0x7C,0x04,0x18,0x04,0x78}, // 6D m
    {0x7C,0x08,0x04,0x04,0x78}, // 6E n
    {0x38,0x44,0x44,0x44,0x38}, // 6F o
    {0x7C,0x14,0x14,0x14,0x08}, // 70 p
    {0x08,0x14,0x14,0x18,0x7C}, // 71 q
    {0x7C,0x08,0x04,0x04,0x08}, // 72 r
    {0x48,0x54,0x54,0x54,0x20}, // 73 s
    {0x04,0x3F,0x44,0x40,0x20}, // 74 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 75 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 76 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 77 w
    {0x44,0x28,0x10,0x28,0x44}, // 78 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 79 y
    {0x44,0x64,0x54,0x4C,0x44}, // 7A z
    {0x00,0x08,0x36,0x41,0x00}, // 7B {
    {0x00,0x00,0x7F,0x00,0x00}, // 7C |
    {0x00,0x41,0x36,0x08,0x00}, // 7D }
    {0x10,0x08,0x08,0x10,0x08}, // 7E ~
};

// Draw one glyph at pixel column x, page row p
static void fbChar(int x, int p, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = kFont[(uint8_t)c - 0x20];
    for (int col = 0; col < 5; col++) {
        if (x + col >= COLS) break;
        uint8_t byte = glyph[col];
        for (int bit = 0; bit < 8; bit++)
            fbPixel(x + col, p * 8 + bit, (byte >> bit) & 1);
    }
    // 1-pixel spacer column
    if (x + 5 < COLS)
        for (int bit = 0; bit < 8; bit++)
            fbPixel(x + 5, p * 8 + bit, false);
}

// Draw string left-aligned from (x, p); returns x after last character
static int fbStr(int x, int p, const char *s)
{
    while (*s) { fbChar(x, p, *s++); x += 6; }
    return x;
}

// Draw string right-aligned so the last character ends at end_x
static void fbStrR(int end_x, int p, const char *s)
{
    fbStr(end_x - (int)strlen(s) * 6, p, s);
}

// Draw graphical soil moisture percentage bar into framebuffer.
static void fbSoilBar(int x, int p, int pct)
{
    pct = (pct < 0) ? 0 : (pct > 100) ? 100 : pct;

    const int W = 60, H = 6, y0 = p * 8 + 1;

    for (int i = 0; i <= W + 1; i++) {
        fbPixel(x + i, y0,         true);
        fbPixel(x + i, y0 + H - 1, true);
    }
    for (int j = 0; j < H; j++) {
        fbPixel(x,         y0 + j, true);
        fbPixel(x + W + 1, y0 + j, true);
    }

    int fill = (pct * W) / 100;
    for (int i = 1; i <= fill; i++)
        for (int j = 1; j < H - 1; j++)
            fbPixel(x + i, y0 + j, true);
}

// Render status top bar (Wi-Fi indicator and digital clock).
static void drawTopBar(bool wifiOk)
{
    memset(sFb[0], 0, COLS);

    fbStr(0, 0, wifiOk ? "WiFi:OK" : "WiFi:--");

    time_t now = time(NULL);
    char tbuf[12];
    if (now > 1000000000L) {
        struct tm *t = gmtime(&now);
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                 t->tm_hour, t->tm_min, t->tm_sec);
    } else {
        snprintf(tbuf, sizeof(tbuf), "--:--:--");
    }
    fbStrR(COLS, 0, tbuf);

    // Separator line at y=9 (bit 1 of page 1)
    for (int x = 0; x < COLS; x++)
        sFb[1][x] |= (1u << 1);
}

// Render Screen A: Plant info, sensor readings, and valve state.
static void drawScreenSensors(void)
{
    fbClearRegion(2, 7);

    char plantName[PLANT_NAME_BUF] = "Unknown";
    storageGetPlantName(plantName, sizeof(plantName));
    char pnBuf[13];
    snprintf(pnBuf, sizeof(pnBuf), "%.12s", plantName);
    fbStr(0, 2, pnBuf);
    char zoneBuf[8];
    snprintf(zoneBuf, sizeof(zoneBuf), "Z:%d", storageGetZoneId());
    fbStrR(COLS, 2, zoneBuf);

    SensorData sd = {0};
    bool valid = sensorGetLatest(&sd);
    if (valid) {
        char buf[20];
        snprintf(buf, sizeof(buf), "T:%dC", sd.temperature);
        fbStr(0, 3, buf);
        snprintf(buf, sizeof(buf), "H:%d%%", sd.humidity);
        fbStrR(COLS, 3, buf);
    } else {
        fbStr(0, 3, "Sensors N/A");
    }

    fbStr(0, 4, "Soil");
    if (valid) {
        fbSoilBar(26, 4, sd.soilMoisture);
        char soilBuf[6];
        snprintf(soilBuf, sizeof(soilBuf), "%d%%", sd.soilMoisture);
        fbStr(90, 4, soilBuf);
    } else {
        fbStr(26, 4, "---");
    }

    fbStr(0, 5, "Valve:");
    fbStr(42, 5, valveIsOpen() ? "OPEN  " : "CLOSED");
}

// Render Screen B: Next schedule details and history session count.
static void drawScreenSchedule(void)
{
    fbClearRegion(2, 7);

    fbStr(0, 2, "Next Schedule");

    IrrigationCommand cmd = {0};
    irrigationGetLastCmd(&cmd);

    if (cmd.command[0] != '\0') {
        fbStr(0, 3, cmd.command);

        char detailBuf[24];
        snprintf(detailBuf, sizeof(detailBuf), "%dmin %dml",
                 cmd.durationMinutes, cmd.waterAmountMl);
        fbStr(0, 4, detailBuf);
    } else {
        fbStr(0, 3, "No Pending Cmd");

        char ts[32] = "Never";
        storageGetLastIrrigation(NULL, NULL, NULL, ts, sizeof(ts));
        ts[16] = '\0';  // trim to fit display
        char lastBuf[24];
        snprintf(lastBuf, sizeof(lastBuf), "Last:%s", ts + 5);
        fbStr(0, 4, lastBuf);
    }

    char countBuf[24];
    snprintf(countBuf, sizeof(countBuf), "Sessions:%lu",
             (unsigned long)storageGetIrrCount());
    fbStr(0, 5, countBuf);

    int dur = 0, vol = 0;
    if (storageGetLastIrrigation(NULL, &dur, &vol, NULL, 0)) {
        char prevBuf[24];
        snprintf(prevBuf, sizeof(prevBuf), "Prev:%dmin %dml", dur, vol);
        fbStr(0, 6, prevBuf);
    }
}

typedef enum { SCREEN_SENSORS, SCREEN_SCHEDULE } Screen;
static EventGroupHandle_t sWifiEvents;

// FreeRTOS display task: periodically refreshes OLED and rotates screens.
static void displayTask(void *arg)
{
    (void)arg;

    // Splash screen
    vTaskDelay(pdMS_TO_TICKS(500));
    fbClear();
    fbStr(14, 2, "IRP Controller");
    fbStr(32, 4, "Starting...");
    fbFlush();
    vTaskDelay(pdMS_TO_TICKS(2000));

    Screen     screen   = SCREEN_SENSORS;
    TickType_t lastFlip = xTaskGetTickCount();
    TickType_t interval = pdMS_TO_TICKS(OLED_SCREEN_INTERVAL_MS);

    while (1) {
        bool wifiOk = !!(xEventGroupGetBits(sWifiEvents) & WIFI_CONNECTED_BIT);

        if ((xTaskGetTickCount() - lastFlip) >= interval) {
            screen   = (screen == SCREEN_SENSORS) ? SCREEN_SCHEDULE : SCREEN_SENSORS;
            lastFlip = xTaskGetTickCount();
        }

        drawTopBar(wifiOk);
        if (screen == SCREEN_SENSORS) drawScreenSensors();
        else                          drawScreenSchedule();

        fbFlush();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Initialize I2C bus, hardware SSD1306 screen, and launch display FreeRTOS task.
void displayInit(EventGroupHandle_t wifi_events)
{
    sWifiEvents = wifi_events;

    Wire.begin(OLED_SDA_GPIO, OLED_SCL_GPIO);
    Wire.setClock(400000);   // 400 kHz fast mode

    oledHwInit();
    fbClear();
    fbFlush();

    Serial0.printf("[display] SSD1306 ready — SDA=%d SCL=%d addr=0x%02X\n",
                   OLED_SDA_GPIO, OLED_SCL_GPIO, OLED_I2C_ADDR);

    xTaskCreate(displayTask, "displayTask", 4096, NULL, 2, NULL);
}