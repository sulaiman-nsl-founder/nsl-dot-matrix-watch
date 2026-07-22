# NSL Dot Matrix Watch

BLE-synced dot-matrix watchface for a XIAO ESP32S3 and a 1.54 inch 200x200 e-paper display.

The watch renders a full ghost-dot grid on the e-paper panel, fills active dots for the current hour and minute, and shows the date below the grid. A small Web Bluetooth companion app syncs the current browser/device time to the watch.

## Hardware

- Seeed Studio XIAO ESP32S3
- WeAct 1.54 inch e-paper display, 200x200
- GxEPD2 display driver: `GxEPD2_154_D67`

## Wiring

| E-paper pin | XIAO ESP32S3 pin |
| --- | --- |
| SDA / MOSI | D10 / GPIO9 |
| SCL / SCK | D8 / GPIO7 |
| CS | D6 / GPIO6 |
| DC | D5 / GPIO5 |
| RES | D4 / GPIO4 |
| BUSY | D2 / GPIO2 |

## Project Structure

```text
firmware/watchface/watchface.ino  Arduino firmware for the watch
web_app/NSL_web.html              Web Bluetooth companion app
docs/PROTOCOL.md                  BLE packet format and UUIDs
```

## Arduino Libraries

Install these from the Arduino Library Manager:

- `GxEPD2`
- ESP32 BLE libraries from the ESP32 board package

Also install the ESP32 board package and select the XIAO ESP32S3 board before uploading.

## BLE Sync

The firmware advertises as:

```text
Nsl Watch
```

The companion app sends a writable text packet:

```text
H:M:S:WD:D:MO:Y
```

Example:

```text
14:35:22:3:22:7:2026
```

## Companion App

Open `web_app/NSL_web.html` in a browser that supports Web Bluetooth.

Recommended:

- Chrome on Android
- Chrome or Edge on desktop

Then click **Connect to Watch** and choose `Nsl Watch`.

## Current Notes

- The web app auto-syncs every 60 seconds while connected.
- The firmware keeps ticking after a sync, but date rollover is not fully handled locally. Regular BLE sync keeps the date correct.
- E-paper refresh is intentionally limited to once per minute to avoid unnecessary full-screen updates.

