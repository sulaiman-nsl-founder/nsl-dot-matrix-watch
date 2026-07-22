/*
 * NSL Dot Matrix Watchface
 * XIAO ESP32S3 + WeAct 1.54" e-ink (200×200, GxEPD2_154_D67)
 *
 * Layout:
 *   Full 20×17 ghost dot grid, edge-to-edge
 *   Hours  → rows 1-7  (2 large digits centred)
 *   Minutes→ rows 10-16 (2 large digits centred)
 *   Date   → text below the dot grid
 *
 * Grid maths (all equal spacing):
 *   DOT_R = 4px, STEP = 10px  → diameter 9px, gap 1px
 *   20 cols: centres at x = 5,15,25…195  (edges 1px–199px)
 *   17 rows: centres at y = 5,15,25…165  (edges 1px–169px)
 *   Digit1 at col 4, Digit2 at col 11    (4-col pad left & right)
 *
 * Wiring (verified working):
 *   SDA(MOSI)→D10(GPIO9) SCL(SCK)→D8(GPIO7)
 *   CS→D6(GPIO6)  DC→D5(GPIO5)  RES→D4(GPIO4)  BUSY→D2(GPIO2)
 */

#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── Pins ─────────────────────────────────────────────────────────
#define CS_PIN   6
#define DC_PIN   5
#define RES_PIN  4
#define BUSY_PIN 2

GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN)
);

// ── BLE ──────────────────────────────────────────────────────────
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcd1234-ab12-cd34-ef56-abcdef123456"

BLEServer*         pServer  = nullptr;
BLECharacteristic* pChar    = nullptr;
bool bleConnected  = false;
bool timeReceived  = false;

// ── Time ─────────────────────────────────────────────────────────
struct { int h=0,m=0,s=0,wd=0,d=1,mo=1,y=2025; } clk;

// ── 5×7 dot font ─────────────────────────────────────────────────
const uint8_t FONT[10][7][5] = {
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,1,1},{1,0,1,0,1},{1,1,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},//0
  {{0,0,1,0,0},{0,1,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,1,1,1,0}},//1
  {{0,1,1,1,0},{1,0,0,0,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{1,1,1,1,1}},//2
  {{1,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{0,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,1,1,1,0}},//3
  {{0,0,0,1,0},{0,0,1,1,0},{0,1,0,1,0},{1,0,0,1,0},{1,1,1,1,1},{0,0,0,1,0},{0,0,0,1,0}},//4
  {{1,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,1,1,1,0}},//5
  {{0,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},//6
  {{1,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{0,1,0,0,0},{0,1,0,0,0}},//7
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},//8
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,1},{0,0,0,0,1},{0,0,0,0,1},{0,1,1,1,0}},//9
};

const char* DAYS[]   = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
const char* MONTHS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                         "JUL","AUG","SEP","OCT","NOV","DEC"};

// ── Grid constants ────────────────────────────────────────────────
#define DOT_R       4     // circle radius
#define STEP       10     // centre-to-centre spacing (equal X and Y)
#define GRID_COLS  20     // 20 × 10 = 200 px wide
#define GRID_ROWS  17     // 17 × 10 = 170 px tall (dot area)
#define ORIG_X      5     // centre of col-0  (5 + 19×10 = 195 → edge at 199)
#define ORIG_Y      5     // centre of row-0  (5 + 16×10 = 165 → edge at 169)

// Digit start columns (col 4 and col 11 → 4 ghost cols each side)
#define D1_COL      4
#define D2_COL     11

// Digit start rows
#define H_ROW       1     // hours:   rows 1-7
#define M_ROW      10     // minutes: rows 10-16

// Inline pixel position helpers
#define CX(col)  (ORIG_X + (col) * STEP)
#define CY(row)  (ORIG_Y + (row) * STEP)

// ── Overlay one digit onto the ghost grid ─────────────────────────
void overlayDigit(int digit, int startCol, int startRow) {
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 5; c++)
      if (FONT[digit][r][c])
        display.fillCircle(CX(startCol + c), CY(startRow + r),
                           DOT_R, GxEPD_WHITE);
}

// ── Full watchface draw ───────────────────────────────────────────
void drawWatchface() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_BLACK);

    // 1. Draw entire ghost grid (outline circles, equal spacing everywhere)
    for (int r = 0; r < GRID_ROWS; r++)
      for (int c = 0; c < GRID_COLS; c++)
        display.drawCircle(CX(c), CY(r), DOT_R, GxEPD_WHITE);

    // Convert hour to 12-hour format for display (1..12)
    int displayHour = clk.h % 12;
    if (displayHour == 0) displayHour = 12;

    // 2. Overlay active digit pixels (filled circles overwrite outlines)
    overlayDigit(displayHour / 10, D1_COL, H_ROW);
    overlayDigit(displayHour % 10, D2_COL, H_ROW);
    overlayDigit(clk.m / 10, D1_COL, M_ROW);
    overlayDigit(clk.m % 10, D2_COL, M_ROW);

    // 3. Date text — centred below dot grid
    //    y=182: bottom of dot area is 169, FreeMonoBold9pt7b baseline offset ~13px
    char buf[28];
    snprintf(buf, sizeof(buf), "%s %02d %s %04d",
             DAYS[clk.wd], clk.d, MONTHS[clk.mo - 1], clk.y);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds(buf, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor((200 - tbw) / 2 - tbx, 192);
    display.print(buf);

  } while (display.nextPage());

  Serial.printf("Updated %02d:%02d  %s %02d/%02d/%04d\n",
    clk.h, clk.m, DAYS[clk.wd], clk.d, clk.mo, clk.y);
}

// ── BLE callbacks ─────────────────────────────────────────────────
class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*)    override { bleConnected = true;  Serial.println("BLE: connected"); }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    s->startAdvertising();
    Serial.println("BLE: disconnected, restarting advert");
  }
};

// Packet: "H:M:S:WD:D:MO:Y"
class CharCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* p) override {
    String v = p->getValue().c_str();
    int h,m,s,wd,d,mo,y;
    if (sscanf(v.c_str(), "%d:%d:%d:%d:%d:%d:%d",
               &h,&m,&s,&wd,&d,&mo,&y) == 7) {
      clk.h=h; clk.m=m; clk.s=s;
      clk.wd=wd; clk.d=d; clk.mo=mo; clk.y=y;
      timeReceived = true;
      Serial.printf("Time: %02d:%02d:%02d\n",h,m,s);
    }
  }
};

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  SPIClass* spi = new SPIClass(FSPI);
  spi->begin(7, 8, 9, 6);
  display.epd2.selectSPI(*spi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 50, false);

  // Boot screen — show the ghost grid so user can see display works
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_BLACK);
    for (int r = 0; r < GRID_ROWS; r++)
      for (int c = 0; c < GRID_COLS; c++)
        display.drawCircle(CX(c), CY(r), DOT_R, GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(28, 192);
    display.print("Waiting for BLE...");
  } while (display.nextPage());

  // BLE
  BLEDevice::init("Nsl Watch");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCB());
  BLEService* svc = pServer->createService(SERVICE_UUID);
  pChar = svc->createCharacteristic(CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pChar->addDescriptor(new BLE2902());
  pChar->setCallbacks(new CharCB());
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE: advertising as 'Nsl Watch'");
}

// ── Loop ──────────────────────────────────────────────────────────
unsigned long lastDraw = 0;
unsigned long lastTick = 0;
bool firstDraw = true;

void loop() {
  unsigned long now = millis();

  // Tick clock every second
  if (timeReceived && now - lastTick >= 1000) {
    lastTick = now;
    clk.s++;
    if (clk.s >= 60) { clk.s=0; clk.m++;
      if (clk.m >= 60) { clk.m=0; clk.h++;
        if (clk.h >= 24) clk.h=0; }}
  }

  // Draw immediately on first time-sync, then every 60s
  if (timeReceived && firstDraw) {
    firstDraw = false;
    lastDraw  = now;
    drawWatchface();
  } else if (timeReceived && now - lastDraw >= 60000) {
    lastDraw = now;
    drawWatchface();
  }
}
