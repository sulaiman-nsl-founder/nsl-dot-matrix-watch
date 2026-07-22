/*
 * NSL Dot Matrix Watchface
 * XIAO nRF54L15 + WeAct 1.54" e-ink (200×200, GxEPD2_154_D67)
 *
 * Board Package : lolren/nrf54-arduino-core (v0.6.x)
 *
 * E-ink Wiring (from nRF54L15 e-ink reference):
 *   BUSY → D0 (PIN_D0)
 *   CS   → D1 (PIN_D1)
 *   DC   → D2 (PIN_D2)
 *   RES  → D3 (PIN_D3)
 *   SCK  → PIN_SPI_SCK
 *   MOSI → PIN_SPI_MOSI
 *
 * BLE Time Sync (nRF Connect / custom app):
 *   Service  UUID : aa9a7856-3412-3412-3412-341278563412
 *   Char UUID     : ab9a7856-3412-3412-3412-341278563412
 *   Write format  : "H:M:S:WD:D:MO:Y"  e.g. "14:35:00:3:15:6:2026"
 *
 * Known nRF54L15 constraints (from lolren core):
 *   - Serial.end() must be called before SPI.begin() (shared peripheral conflict)
 *   - SPI.setPins() must be called before SPI.begin()
 *   - display.init(0) — no extra args on this core
 *   - BLE uses bluefruit.h compat layer, NOT BLEDevice/BLEServer (ESP32 API)
 *   - No Serial.printf() — use Serial.print() chains
 *
 * How to sync time via nRF Connect app:
 *   1. Scan → connect to "Nsl Watch"
 *   2. Find the time characteristic [W WNR]
 *   3. Write (UTF-8 / Text) → "14:35:00:3:15:6:2026"
 *      fields: hour:min:sec:weekday(0=Sun):day:month:year
 *   4. Watch redraws immediately, then every 60 s
 */

#define ENABLE_GxEPD2_GFX 0
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <bluefruit.h>

// ── E-ink pins ────────────────────────────────────────────────────────────────
#define CS_PIN   PIN_D1
#define DC_PIN   PIN_D2
#define RES_PIN  PIN_D3
#define BUSY_PIN PIN_D0

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN)
);

// ── BLE UUIDs ─────────────────────────────────────────────────────────────────
#define TIME_SERVICE_UUID  "12345678-1234-1234-1234-123456789aaa"
#define TIME_CHAR_UUID     "12345678-1234-1234-1234-123456789aab"

BLEService        timeSvc(TIME_SERVICE_UUID);
BLECharacteristic timeChar(TIME_CHAR_UUID);

// ── BLE state ─────────────────────────────────────────────────────────────────
static uint16_t activeConn   = BLE_CONN_HANDLE_INVALID;
static uint32_t lastNotifyMs = 0;

// ── Clock state ───────────────────────────────────────────────────────────────
struct {
  int h=10, m=25, s=0, wd=1, d=27, mo=4, y=2026;
} clk;

static bool timeReceived = false;
static bool needsRedraw  = false;

// ── 5×7 dot-matrix font ───────────────────────────────────────────────────────
const uint8_t FONT[10][7][5] = {
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,1,1},{1,0,1,0,1},{1,1,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  {{0,0,1,0,0},{0,1,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,1,1,1,0}},
  {{0,1,1,1,0},{1,0,0,0,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{1,1,1,1,1}},
  {{1,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{0,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,1,1,1,0}},
  {{0,0,0,1,0},{0,0,1,1,0},{0,1,0,1,0},{1,0,0,1,0},{1,1,1,1,1},{0,0,0,1,0},{0,0,0,1,0}},
  {{1,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,1,1,1,0}},
  {{0,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  {{1,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{0,1,0,0,0},{0,1,0,0,0}},
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,1},{0,0,0,0,1},{0,0,0,0,1},{0,1,1,1,0}},
};

const char* DAYS[]   = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
const char* MONTHS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                         "JUL","AUG","SEP","OCT","NOV","DEC"};

// ── Grid constants ────────────────────────────────────────────────────────────
#define DOT_R      4
#define STEP      10
#define GRID_COLS 20
#define GRID_ROWS 17
#define ORIG_X     5
#define ORIG_Y     5
#define D1_COL     4
#define D2_COL    11
#define H_ROW      1
#define M_ROW     10
#define CX(col)  (ORIG_X + (col) * STEP)
#define CY(row)  (ORIG_Y + (row) * STEP)

// ── Display helpers ───────────────────────────────────────────────────────────
void overlayDigit(int digit, int startCol, int startRow) {
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 5; c++)
      if (FONT[digit][r][c])
        display.fillCircle(CX(startCol + c), CY(startRow + r),
                           DOT_R, GxEPD_WHITE);
}

void drawWatchface() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_BLACK);

    // Ghost dot grid
    for (int r = 0; r < GRID_ROWS; r++)
      for (int c = 0; c < GRID_COLS; c++)
        display.drawCircle(CX(c), CY(r), DOT_R, GxEPD_BLACK);

    // Convert hour to 12-hour format for display (1..12)
    int displayHour = clk.h % 12;
    if (displayHour == 0) displayHour = 12;

    // Active digit pixels (filled circles overwrite outlines)
    overlayDigit(displayHour / 10, D1_COL, H_ROW);
    overlayDigit(displayHour % 10, D2_COL, H_ROW);
    overlayDigit(clk.m / 10, D1_COL, M_ROW);
    overlayDigit(clk.m % 10, D2_COL, M_ROW);

    // Date text centred below dot grid
    char buf[28];
    snprintf(buf, sizeof(buf), "%s %02d %s %04d",
             DAYS[clk.wd], clk.d, MONTHS[clk.mo - 1], clk.y);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    int16_t  tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor((200 - tbw) / 2 - tbx, 192);
    display.print(buf);

  } while (display.nextPage());

  display.hibernate();   // panel deep sleep between redraws (saves power)

  // Convert hour to 12-hour format for serial log
  int displayHourLog = clk.h % 12;
  if (displayHourLog == 0) displayHourLog = 12;

  Serial.print("[EPD] Updated ");
  Serial.print(displayHourLog); Serial.print(":");
  if (clk.m < 10) Serial.print("0");
  Serial.println(clk.m);
}

void drawBootScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_BLACK);
    for (int r = 0; r < GRID_ROWS; r++)
      for (int c = 0; c < GRID_COLS; c++)
        display.drawCircle(CX(c), CY(r), DOT_R, GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(20, 192);
    display.print("Waiting BLE...");
  } while (display.nextPage());
  display.hibernate();
}

// ── BLE write callback ────────────────────────────────────────────────────────
// Packet: "H:M:S:WD:D:MO:Y"  e.g. "14:35:00:3:15:6:2026"
void timeWriteCallback(uint16_t conn_handle,
                       BLECharacteristic* chr,
                       uint8_t* data,
                       uint16_t len)
{
  (void)conn_handle;
  (void)chr;
  if (len < 1 || len > 32) return;

  char buf[33];
  memcpy(buf, data, len);
  buf[len] = '\0';

  int h, m, s, wd, d, mo, y;
  if (sscanf(buf, "%d:%d:%d:%d:%d:%d:%d", &h, &m, &s, &wd, &d, &mo, &y) == 7) {
    clk.h=h; clk.m=m; clk.s=s;
    clk.wd=wd; clk.d=d; clk.mo=mo; clk.y=y;
    timeReceived = true;
    needsRedraw  = true;
    int displayH = h % 12;
    if (displayH == 0) displayH = 12;
    Serial.print("[BLE] Time set ");
    Serial.print(displayH); Serial.print(":");
    if (m < 10) Serial.print("0");
    Serial.println(m);
  } else {
    Serial.print("[BLE] Bad packet: ");
    Serial.println(buf);
  }
}

// ── BLE connection callbacks ──────────────────────────────────────────────────
void connectCallback(uint16_t conn_handle)
{
  activeConn   = conn_handle;
  lastNotifyMs = millis();
  Serial.print("[BLE] Connected, handle=");
  Serial.println(conn_handle);
}

void disconnectCallback(uint16_t conn_handle, uint8_t reason)
{
  (void)conn_handle;
  activeConn = BLE_CONN_HANDLE_INVALID;
  Serial.print("[BLE] Disconnected reason=0x");
  Serial.println(reason, HEX);
  Bluefruit.Advertising.start(0);
  Serial.println("[BLE] Advertising restarted");
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("=== NSL Watch nRF54L15 ===");

  // ── E-ink init ────────────────────────────────────────────────────────────
  // Serial.end() is REQUIRED before SPI.begin() on nRF54L15 (peripheral conflict)
  Serial.end();

  SPI.setPins(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  SPI.begin();

  display.init(0);        // no baud-rate arg on lolren core
  display.setRotation(0);
  drawBootScreen();

  Serial.begin(115200);
  delay(200);
  Serial.println("[EPD] Boot screen done");

  // ── BLE stack ─────────────────────────────────────────────────────────────
  Bluefruit.begin();
  Bluefruit.setName("NslWa");
  Bluefruit.setTxPower(4);

  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  // ── GATT service ──────────────────────────────────────────────────────────
  timeSvc.begin();

  timeChar.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY);
  timeChar.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);  // (read, write)
  timeChar.setMaxLen(32);
  timeChar.setWriteCallback(timeWriteCallback);
  timeChar.begin();

  // ── Advertising ───────────────────────────────────────────────────────────
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(timeSvc);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);

  Serial.println("[BLE] Advertising as 'Nsl Watch'");
  Serial.println("[BLE] Write format:  H:M:S:WD:D:MO:Y");
  Serial.println("[BLE] Example:       14:35:00:3:15:6:2026");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
static unsigned long lastTickMs = 0;
static unsigned long lastDrawMs = 0;

void loop()
{
  unsigned long now = millis();

  // 1-second software tick
  if (timeReceived && now - lastTickMs >= 1000UL) {
    lastTickMs = now;
    clk.s++;
    if (clk.s >= 60) {
      clk.s = 0; clk.m++;
      if (clk.m >= 60) {
        clk.m = 0; clk.h++;
        if (clk.h >= 24) clk.h = 0;
      }
    }
  }

  // Redraw: immediately on first/fresh BLE sync, then every 60 s
  if (needsRedraw || (timeReceived && now - lastDrawMs >= 60000UL)) {
    needsRedraw = false;
    lastDrawMs  = now;
    drawWatchface();
  }

  // BLE keep-alive NOTIFY every 10 s — prevents Android GATT timeout (0x08)
  if (activeConn != BLE_CONN_HANDLE_INVALID) {
    if (now - lastNotifyMs >= 10000UL) {
      lastNotifyMs = now;
      uint8_t ping = (uint8_t)clk.m;
      timeChar.notify(activeConn, &ping, 1);
    }
  }

  delay(100);
}
