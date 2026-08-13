# LightCraft

A 3-gang smart light switch running on the **Guition ESP32-4848S040** — an
ESP32-S3 with a 480×480 IPS display (ST7701 RGB panel) and GT911 capacitive
touch — built on the [EmbeddedGFX](https://github.com/BrandonVP/EmbeddedGFX)
library.

## Tabs

- **Home** — date / time / weather. Placeholder for now; the NTP clock and
  weather API (reusing keys from another project) are a future feature.
- **Switches** — three ON/OFF light toggles in a row.
- **Settings** — theme picker.

**Behaviour:** turning a light on from the Switches tab returns to the Home tab
30 seconds later.

## Hardware

- **Guition ESP32-4848S040** (ESP32-S3-WROOM-1, N16R8 — 16 MB flash / 8 MB PSRAM).
- 480×480 IPS, **ST7701** RGB panel via **Arduino_GFX**, backlight on GPIO 38.
- **GT911** capacitive touch on I²C SDA 19 / SCL 45.
- Light relays on **GPIO 40, 2, 1** (active-high) — from the seller's
  `86switch_onoff` demo (`doMain.cpp`). Set in `RelayControl.cpp`.

Only `ArduinoGFXAdapter.h` and `GT911Adapter.h` are hardware-specific; the app
layer is plain EmbeddedGFX.

## Prerequisites (Arduino libraries)

From the seller package / upstream:
- **Arduino_GFX** (moononournation)
- **TAMC_GT911** touch library
- ESP32 Arduino core (with the ESP32-S3 board support)

## The EmbeddedGFX library is a git submodule

Pulled in at `LightCraft/Libraries/EmbeddedGFX`, pinned to a commit that includes
the resolution-independent (480×480) layout support.

```bash
git clone --recurse-submodules <this-repo-url>
# already cloned without it?
git submodule update --init --recursive
```

## Build & upload — IMPORTANT board options

Open `LightCraft.sln`, and in the Visual Micro board selector choose the
**ESP32S3 Dev Module** with these options (the panel framebuffer needs PSRAM):

- **PSRAM: OPI PSRAM**  ← required, or the display will not initialise
- Flash Size: **16 MB**
- Partition scheme with room for the app (e.g. *16M Flash (3MB APP / 9.9MB FATFS)*)
- USB CDC On Boot: your preference

Then build and upload over the USB/UART port.

## Roadmap

- Home tab: NTP time/date + weather API (keys come from another project).
- Optional: physical-button input, schedules, MQTT/Home Assistant.
