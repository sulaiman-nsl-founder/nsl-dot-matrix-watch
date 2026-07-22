# NSL Dot Matrix Watch

BLE-synced dot-matrix watchface for a XIAO nRF54L15 and a 1.54 inch 200x200 e-paper display.

The watch renders a full ghost-dot grid on the e-paper panel, fills active dots for the current hour and minute, and shows the date below the grid. A small Web Bluetooth companion app syncs the current browser/device time to the watch.

## Hardware

- Seeed Studio XIAO nRF54L15
- WeAct 1.54 inch e-paper display, 200x200
- GxEPD2 display driver: `GxEPD2_154_D67`

## Wiring

| E-paper pin | XIAO nRF54L15 pin |
| --- | --- |
| BUSY | D0 / `PIN_D0` |
| CS | D1 / `PIN_D1` |
| DC | D2 / `PIN_D2` |
| RES | D3 / `PIN_D3` |
| SCK | `PIN_SPI_SCK` |
| MOSI | `PIN_SPI_MOSI` |

## Project Structure

```text
firmware/NSL_Watch_nRF54L15/NSL_Watch_nRF54L15.ino  Current Arduino firmware for XIAO nRF54L15
firmware/watchface/watchface.ino                     Legacy ESP32S3 firmware
web_app/NSL_web.html                                 Web Bluetooth companion app
docs/PROTOCOL.md                                     BLE packet format and UUIDs
```

## Arduino Libraries

Use the `lolren/nrf54-arduino-core` board package for XIAO nRF54L15.

Install these from the Arduino Library Manager:

- `GxEPD2`
- Bluefruit compatibility support from the nRF54 board core

Select the XIAO nRF54L15 board before uploading.

## BLE Sync

The firmware advertises as:

```text
NslWa
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

Then click **Connect to Watch** and choose `NslWa`.

## GitHub Pages Hosting

This repository includes a root `index.html` that redirects to the companion app, so it can be hosted directly with GitHub Pages.

After pushing to GitHub:

1. Open the repository on GitHub.
2. Go to **Settings > Pages**.
3. Set **Source** to **Deploy from a branch**.
4. Select branch `main` and folder `/root`.
5. Save.

Your hosted app will be available at:

```text
https://YOUR_USERNAME.github.io/YOUR_REPOSITORY/
```

## Current Notes

- The web app auto-syncs every 60 seconds while connected.
- The firmware keeps ticking after a sync, but date rollover is not fully handled locally. Regular BLE sync keeps the date correct.
- E-paper refresh is intentionally limited to once per minute to avoid unnecessary full-screen updates.
