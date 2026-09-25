# LightCraft

A 3-gang smart light switch running on the **Guition ESP32-4848S040** — an
ESP32-S3 with a 480×480 IPS display (ST7701 RGB panel) and GT911 capacitive
touch — built on the [EmbeddedGFX](https://github.com/BrandonVP/EmbeddedGFX)
library.

## Tabs

- **Home** — two cards: an NTP clock (time + date) on top, and a weather card
  below with a drawn condition icon, the current outdoor reading from
  OpenWeatherMap, and the room reading from the LAN weather station. **Tap the
  weather card** for the 5-day forecast.
- **Control** — the room mini-split on top, the three light toggles along the
  bottom.
- **Settings** — theme picker, temperature rules, WiFi.

**Behaviour:** turning a light on from the Control tab returns to the Home tab
30 seconds later. Any further tap on that tab pushes the timer out, so adjusting
the mini-split is never interrupted mid-edit.

## Mini-split control (Control tab) — IN PROGRESS

> The UI, the shadow state and the save/coalesce logic are done. The transport
> is not: `transmitState()` in `MiniSplit.cpp` only logs the frame it would
> send, so the status line reads **no link** and nothing reaches the unit yet.

The room unit is a **Della Vario (TL) `048-TL-18K2VB-21S-IN`**, driven over IR by
a separate blaster node (M5StickS3 + Grove IR unit) reached over ESP-NOW. That
hardware is on order. The WiFi/Tuya and AUX-serial routes were both rejected —
Della's firmware drops WiFi every ~3 months and needs a re-pair (which rotates
the local key), and this exact model reports zero frames on the AUX serial
protocol.

The panel exposes setpoint (60–86 °F), mode (off / heat / cool / auto), fan
(auto / low / med / high) and vertical + horizontal blade movement, split across
two levels.

**OFF is the first entry in the mode row, not a separate power button** — one
control, so there is never a power switch and a mode disagreeing on screen.
Underneath, `MiniSplit` still keeps power and mode as separate fields, because
IR protocols encode them separately and because that way the running mode
survives being switched off and comes back on its own.

- The **Control tab** carries a large setpoint readout and the mode row —
  `OFF | HEAT | COOL | AUTO` — because those are the daily job. A **`>` in the
  top-right corner** marks it as openable, the same cue the Home weather card
  uses; **tapping the card** opens the full page. The buttons on the card still
  work as buttons, since the card face is hit-tested underneath them.
- The **full page** (`ClimateApp`) takes the whole screen for mode, fan and both
  blade axes, with room left for whatever the IR protocol turns out to expose.

That split is also what pays for the much larger light toggles at the bottom of
the Control tab.

Two things follow from IR being **open loop**:

- A remote sends its *complete* state on every press and the unit never answers,
  so `MiniSplit.*` keeps a shadow copy and re-sends everything on any change.
  Edits are coalesced after a 700 ms settle (three taps on the setpoint is one
  frame, not three) and the state is saved to NVS so a reboot does not forget
  it. It is also re-sent every 10 minutes in case a frame was missed.
- The shadow **drifts** whenever the handheld remote or the Della app is used,
  and the panel cannot tell. The planned fix is the node's IR receiver: decode
  the remote's frames and push the real state back.

The IR protocol is not identified yet — Della's older units are AUX (→ Electra
in IRremoteESP8266), but the TL's TCL-branded WiFi board suggests the TCL family
instead. An `IRrecvDumpV3` capture of the handheld remote settles it. Blade
control is therefore modelled as fixed/swing with room to grow: the fields are
`uint8_t`, not `bool`, in case the unit has discrete positions.

## Weather (Home) and the 5-day forecast

The weather card is a button: tapping it opens the forecast page (registered
under the Home menu, so the Home tab stays lit and **Back** returns to it) and
asks the network task for a fresh fetch on the way in.

The forecast comes from OpenWeatherMap's free **5 day / 3 hour** endpoint
(`/data/2.5/forecast`) — the daily One Call feed needs its own subscription — so
the 3-hourly slots are folded into local calendar days: high and low are the
extremes across each day's slots, and the icon is taken from the slot nearest
1 pm. Day 0 is today, so its range only covers the hours still to come. It
refreshes every 30 minutes, plus on demand when the page opens, and is skipped
until NTP has synced (the day a slot belongs to comes from the local clock).

### Icons

`WeatherIcons.cpp` **draws** the condition icons — sun, moon, sun/moon behind
cloud, cloud, overcast, showers, rain, storm, snow, mist — from circles, lines
and spans rather than storing bitmaps. That way one icon serves both sizes
(92 px on the Home card, 52 px in the forecast rows), the outline picks itself
from the luminance of the card behind it so the icons work on the dark themes
*and* Snow, and it costs no flash. Each shape is painted twice — inflated in the
outline colour, then at size in its fill — which outlines the union of
overlapping circles without leaving interior arcs.

This is the one app-layer module that calls Arduino_GFX directly: EmbeddedGFX's
`IDisplay` carries only rect/round-rect primitives, and these need circles and
lines. `wicon_begin(gfx)` in `setup()` binds the surface.

## WiFi setup (Settings > WiFi)

The network is picked on the panel, not compiled in. The page shows the current
link, scans for networks, and on a tap opens the library's on-screen keyboard
for the password. What you choose is saved to NVS and the network task
re-associates with it — no reflash.

```
Connected: MyNetwork
192.168.68.52   -58 dBm

MyNetwork            *  -58
Neighbour-5G         *  -71
CoffeeShop              -80

[ SCAN ]  [ PAGE 1/2 ]  [ FORGET ]
```

`*` marks a secured network; the number is dBm. Open networks are saved and
joined without asking for a password. **FORGET** drops the saved network and
falls back to `secrets.h`.

`WIFI_SSID` / `WIFI_PASSWORD` in `secrets.h` are now a **fallback, not the
source of truth**: they are used only until a network is chosen on the panel, so
a freshly flashed board still comes up exactly as before. Leave them blank if
you would rather set the network on the device.

Scanning is asynchronous (`WiFi.scanNetworks(true)`) and collected in the tick —
a synchronous scan would block the UI loop for seconds. Note that scanning makes
the radio hop channels, so a weather fetch in flight may fail; it simply retries.

### The on-screen keyboard

`KeyboardApp` lives in **EmbeddedGFX**, not here, so any project on the library
gets it. A 10x4 grid with lowercase, uppercase and symbol layers, plus shift,
layer, space, backspace, cancel and accept; up to 64 characters, optionally
masked. It lays itself out from the display's real dimensions, so it works on a
480x480 or a 480x320 panel.

It is registered on `MENU_hidden` — a menu with no tab — so it never shows up in
the generated Settings list. A caller arms it and switches to it:

```c
KeyboardApp_open("Password: MyAP", "", 63, true, APP_WIFI, onPasswordEntered);
app->newApp(APP_KEYBOARD);
```

It needs 48 app-button slots, which is why `GFX_APP_BUTTON_SIZE` is 56. There is
a `static_assert` in `WiFiApp.cpp` guarding that.

## Temperature rules (Settings > Temp Rules)

Each switch can be driven by the room temperature published by the ESP8266
weather station on the LAN. One row per light:

```
Fan     [ ABOVE ]   [ - ]  74°  [ + ]
```

- **Mode** cycles `OFF` → `ABOVE` → `BELOW`. `ABOVE` turns the light on when the
  room reaches the setpoint (a ceiling fan); `BELOW` is the inverse (a heater).
- **- / +** move the setpoint between 40 °F and 95 °F. Hold to repeat; keep
  holding to move 5 °F at a time.
- **SAVE** writes the rules to flash. An edit takes effect immediately — saving
  is only what makes it survive a reboot.

Rules are **edge triggered**: a light is switched when the temperature crosses
the setpoint, never held there. A manual tap on the Control tab therefore
always wins until the next crossing, and a light already under a rule shows it
on its Control-tab label (`Fan >74°`). A 2 °F deadband on the release side keeps a
reading that hovers on the setpoint from chattering the relay, and the rules
stop acting entirely if the room reading is missing or more than 5 minutes old.

**Storage:** the rules live in the ESP32's NVS flash (`Preferences`, namespace
`lightcraft`), not on the SD card — the TF slot shares its SPI bus (IO47/IO48)
with the ST7701 panel's command lines, and flash needs no card inserted.

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

Flash Mode **QIO** and Flash Speed **80 MHz** matter here beyond raw speed — see
below.

## Known issue: the image steps sideways for a frame

Every so often the whole screen shifts right and snaps back, on any page. This
is the RGB peripheral's line FIFO running dry, not a drawing bug.

The framebuffer lives in PSRAM (it has to — 480x480x2 = 460 KB) and the LCD
streams it out continuously. On the ESP32-S3, **flash and PSRAM share the MSPI
bus**, so anything that stalls that bus starves the FIFO: a cache miss on a cold
code path (WiFi callbacks, HTTP, JSON parsing, an NVS write), or PSRAM traffic
from the other core. Pixels arrive late, the line draws shifted, and the next
frame recovers.

The proper fix — a bounce buffer, so the DMA reads from internal SRAM — is not
available here: its refill ISR is not IRAM-safe in the stock Arduino build and
faults under exactly the flash-cache-miss conditions that cause the problem
(that crash is why `bounce_buffer_size_px` is 0). What is left:

1. **Lower the pixel clock** — `PANEL_PCLK_HZ` at the top of `LightCraft.ino`.
   Less continuous bandwidth means more slack to ride out a stall.
   16 MHz ~= 56 Hz, 14 MHz ~= 49 Hz, 12 MHz ~= 42 Hz refresh. Currently 14 MHz.
2. **Flash Mode QIO / Flash Speed 80 MHz** in the board options. Slower flash
   means every cache miss stalls the shared bus for longer.
3. To confirm the cause, set `WEATHER_ENABLE 0` and run for a while. If the
   shifting stops or gets much rarer, it is the network task's flash traffic.

## Roadmap

- Optional: physical-button input, time-of-day schedules, MQTT/Home Assistant.
- Temp rules: per-rule hysteresis and editable light names.
- Forecast: hourly detail, and wind / precipitation chance per day.
