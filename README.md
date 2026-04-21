# Accurate Measurement of Bodily Fluid for Patient Monitoring
### Ohio State University Capstone — Group 1.2 (BME) + CSE Team

**BME Team:** Andrew DePuy, Omar Mitiek, Nagisa Muise, Saanil Rao, Bridget Yu  
**CSE Team:** Jim Cai, Morgan Franke, Jason Lin, Roman Sarkis  
**Clinical Advisors:** Dr. Ahmed Aly, Dr. Casey Walk, Jake Rumberger, Dhiaeddine Djabri, Tony Boualoy  
**Capstone Advisors:** Clarissa Belloni, Mark Ruegsegger

---

## Table of Contents

1. [Project Background](#1-project-background)
2. [Clinical Context](#2-clinical-context)
3. [System Overview](#3-system-overview)
4. [Hardware](#4-hardware)
   - [Components List](#components-list)
   - [Wiring Reference](#wiring-reference)
5. [Repository Structure](#5-repository-structure)
6. [Software Architecture](#6-software-architecture)
   - [Combined Main Sketch](#combined-main-sketch)
   - [Weight / Load Cell Pipeline](#weight--load-cell-pipeline)
   - [Color Sensor Pipeline](#color-sensor-pipeline)
   - [Display and UI](#display-and-ui)
   - [Alert System](#alert-system)
   - [Time Sync](#time-sync)
7. [Getting Started](#7-getting-started)
   - [Arduino Library Dependencies](#arduino-library-dependencies)
   - [Flashing the Firmware](#flashing-the-firmware)
   - [Load Cell Calibration](#load-cell-calibration)
   - [Color Sensor Calibration](#color-sensor-calibration)
   - [Time Sync Setup](#time-sync-setup)
8. [Key Tunable Constants](#8-key-tunable-constants)
9. [Alert Logic Reference](#9-alert-logic-reference)
10. [Serial Command Reference](#10-serial-command-reference)
11. [Testing Plan](#11-testing-plan)
12. [Known Issues and Limitations](#12-known-issues-and-limitations)
13. [Design Decisions and Trade-offs](#13-design-decisions-and-trade-offs)
14. [Future Work](#14-future-work)
15. [Related Resources](#15-related-resources)

---

## 1. Project Background

This project is a joint BME + CSE capstone at Ohio State. The goal is to build a bedside device that automates continuous urine output measurement for ICU patients, replacing the current manual process where nurses visually read collection bag markings every 1–2 hours.

The device hangs a standard Foley drainage bag from an internal load cell to continuously weigh the bag and convert grams to milliliters. A spectral color sensor (AS7341) inserted inline in the tubing detects qualitative abnormalities in urine color (blood, dark amber, pink). Readings are shown on a touchscreen mounted on the device enclosure, with audible and visual alerts for abnormal conditions.

### What came before this code

Previous OSU capstone teams (2017, 2020, 2021) each attempted similar devices. The 2017 project used a hanging load cell but suffered from calibration drift. The 2020 project used optical fluid-level sensing inside a clear chamber — effective but hard to clean. The 2021 project used capacitance sensing with Bluetooth, achieving near-100% bench precision but was difficult to sterilize and not compatible with standard Foley bags.

This iteration combines weight-based volume measurement with spectral color sensing, keeping the standard bag intact and external to the electronics — avoiding the sterilization and compatibility issues of prior designs.

Commercial devices like the Accuryn® (Potrero Medical) and BD Sensica™ solve similar problems but cost far more, require proprietary catheters, and are not accessible for most hospitals. This project is positioned as a low-cost, retrofit-compatible alternative.

---

## 2. Clinical Context

ICU nurses need to:
- Track urine output volume continuously (not just hourly)
- Detect sudden drops (bag accidentally emptied or disconnected)
- Detect slow leaks (either at the bag's drain valve or urimeter valve)
- Catch the silent-leak case where a drain valve was left open from the start and no urine ever accumulates
- Know when the bag is near full or full
- Be alerted when urine color indicates blood, infection, or severe dehydration

The clinical accuracy target is **±10 mL** from true volume across the 0–3,000 mL bag range. The color sensor's acceptability threshold is **>95% correct classification** of normal vs. abnormal samples.

A normal adult ICU patient produces roughly 0.5–1 mL/kg/hr of urine. Oliguria (< 0.5 mL/kg/hr) is an early indicator of acute kidney injury or sepsis, so frequent, accurate measurement matters.

Nurses at Wexner Medical Center specifically requested:
- Readings visible from the doorway
- Audible alerts for abnormal color and bag fill status
- A dismissible alert system that doesn't cause alarm fatigue
- Visual color/hydration status display (not just volume)

---

## 3. System Overview

```
┌──────────────────────────────────────────────────────┐
│                   Arduino Mega 2560                  │
│                                                      │
│  Load Cell → HX711 ADC → weight filtering → volume   │
│                                                      │
│  AS7341 color sensor → hardcoded calibration         │
│                      → 5-anchor classification       │
│                                                      │
│  RA8875 480×272 touchscreen → volume display         │
│                             → color / alert panels   │
│                             → output log / alert log │
│                                                      │
│  Speaker → audible alerts (tone frequency by type)   │
│                                                      │
│  Serial (USB) → time sync from Python script         │
│              → debug output                          │
└──────────────────────────────────────────────────────┘
```

The device runs a single combined `.ino` sketch (`urine_monitor_combined_iteration8.ino`) on the Arduino Mega 2560. There is no wireless communication or cloud integration at this stage — all data is local to the device and displayed on the touchscreen.

---

## 4. Hardware

### Components List

| Component | Part | Notes |
|---|---|---|
| Microcontroller | Arduino Mega 2560 | Main compute; 5V logic |
| Load cell amplifier | HX711 ADC breakout | Soldered connections required — breadboard contacts are unreliable |
| Load cells | Two half-bridge load cells (wired together) | Wired as a combined full bridge into the HX711 |
| Touchscreen | Adafruit RA8875 480×272 TFT with resistive touch | SPI; includes touch controller on breakout |
| Color / spectral sensor | Adafruit AS7341 10-channel breakout | I2C; measures 415–680 nm across 8 channels + clear + NIR |
| Speaker | Passive piezo speaker | Driven by `tone()` on pin 8 |
| Power | USB from laptop or 5V wall adapter | No battery at this stage |

> **Important:** The original AMS-Osram AS7341 chip (bare IC) was tried first and abandoned — it is too small to wire by hand and lacks the support circuitry. Always use the **Adafruit AS7341 breakout board**, not the bare chip.

### Wiring Reference

#### HX711 → Arduino Mega

| HX711 Pin | Arduino Mega Pin |
|---|---|
| VCC | 5V |
| GND | GND |
| DT (Data) | Digital 4 |
| SCK (Clock) | Digital 5 |

#### RA8875 Touchscreen → Arduino Mega (SPI)

| RA8875 Pin | Arduino Mega Pin |
|---|---|
| VCC | 5V |
| GND | GND |
| SCK | 52 (SPI SCK) |
| MISO | 50 (SPI MISO) |
| MOSI | 51 (SPI MOSI) |
| CS | 10 |
| RST | 9 |

#### AS7341 Color Sensor → Arduino Mega (I2C)

| AS7341 Pin | Arduino Mega Pin |
|---|---|
| VCC | 5V |
| GND | GND |
| SDA | 20 (I2C SDA) |
| SCL | 21 (I2C SCL) |

#### Speaker

| Speaker | Arduino Mega Pin |
|---|---|
| + | Digital 8 |
| − | GND |

---

## 5. Repository Structure

```
AccurateMeasurementofBodyFluidForPatientMonitoring/
│
├── README.md                                 ← You are here
│
├── urine_monitor_combined_iteration8.ino     ← Primary combined firmware
│
├── AS7341_calibration.ino                    ← Standalone AS7341 calibration utility
│                                               (run this to capture color references,
│                                                then paste the output into iteration 8)
│
├── send_time.py                              ← Python script that syncs real-time clock
│                                               to the Arduino over USB Serial
│
├── LoadCellCalibrationInstructions.pdf       ← Step-by-step load cell calibration guide
├── ColorSensorCalibrationInstructions.pdf    ← Step-by-step color sensor calibration guide
│
└── libraries.zip                             ← All Arduino libraries bundled
                                                (HX711_ADC, RA8875, GFX, AS7341, TimeLib)
```

> **Naming convention:** The combined main sketch is versioned by iteration number. Iteration 8 is the final capstone version. If a future continuation team increments to iteration 9+, always work from the highest-numbered file and keep prior iterations as a history reference.

---

## 6. Software Architecture

### Combined Main Sketch

The combined sketch (`urine_monitor_combined_iteration8.ino`) is the only file that needs to be flashed for normal operation. It integrates:

- Load cell weight reading and filtering
- Color sensor measurement (calibration values hardcoded at the top of the file)
- RA8875 touchscreen UI with touch handling
- Alert system with audible tones (5 alert types)
- Output event log and alert event log
- Real-time clock (synced via Serial from `send_time.py`)
- Leak detection (volume drop trend)
- Extended-zero-output detection (silent leak / valve-left-open)
- Hourly output logging

The `loop()` runs the following in order each cycle (with a 50ms delay):

```
handleSerialInput()   → time sync + color diagnostic commands
updateWeightLogic()   → read scale, filter, detect events
pollColorSensor()     → read AS7341 every 3 seconds
drawClock()           → refresh the live clock on screen
checkHourlyOutputLog()→ log cumulative hourly output
checkTouchButtons()   → handle DISMISS / RESET / VIEW touch
playAlertSound()      → repeat beep while alert is active
```

### Weight / Load Cell Pipeline

1. `scale.update()` / `scale.getData()` — raw gram reading from HX711_ADC library
2. **Stability filter:** a reading is only accepted after 5 consecutive readings differ by less than `THRESHOLD` (5.0 g)
3. **Downward trend guard:** decreases only take effect after 8 consecutive downward stable readings (`DOWNWARD_COUNT_REQUIRED`). This prevents momentary bumps from resetting the displayed volume
4. **Drop-to-zero detection:** if `displayedWeight > 50g` and `currentWeight < 20g`, a void event is declared — the DROP alert fires and the baseline resets
5. **Volume add event:** if volume increases by ≥ `ADD_EVENT_THRESHOLD_ML` (25 mL) AND the `ADD_EVENT_COOLDOWN_MS` (90 seconds) has elapsed since the last logged event, an output log entry is written. The baseline (`previousDisplayedVolumeMl`) only advances when an event actually fires — this ensures `addedMl` reflects the full accumulation since the last log, not just the most recent small jump
6. Volume in mL = weight in grams × `GRAMS_TO_ML` (1.0 — urine density ≈ water)

**Key insight for future debugging:** The `calibrationFactor` constant (currently `210.036758`) is load-cell-specific. If you replace or re-mount the load cell hardware, you must re-run the HX711 calibration example sketch (see `LoadCellCalibrationInstructions.pdf`) and update this value at the top of the combined sketch.

### Color Sensor Pipeline

The AS7341 measures 10 channels of spectral light (415–680 nm + Clear + NIR). In iteration 8, the entire calibration lives in a hardcoded block at the top of the combined sketch — no interactive calibration is needed at boot.

**Hardcoded calibration block:**

Six reference spectra live in `HARDCODED_*_REF[10]` arrays near the top of the file:
- `HARDCODED_DARK_REF` — sensor's dark current / noise floor
- `HARDCODED_COLORLESS_REF` — distilled water / empty tube baseline
- `HARDCODED_PALE_YELLOW_REF` — well-hydrated (normal)
- `HARDCODED_YELLOW_REF` — normal urine
- `HARDCODED_DARK_YELLOW_REF` — dehydrated / concentrated (abnormal)
- `HARDCODED_PINK_REF` — blood-tinged / hematuria (abnormal)

Plus `HARDCODED_COLOR_GAIN` and `HARDCODED_LED_CURRENT_MA` for the sensor's operating settings.

These are captured by `AS7341_calibration.ino` (standalone sketch) and pasted into the combined sketch. At `setup()`, `loadHardcodedColorCalibration()` copies them into the working reference arrays and enables measurement immediately. See [Color Sensor Calibration](#color-sensor-calibration) below.

**Per-reading measurement:**

1. Take 12 averaged samples with outlier rejection (readings more than 2500 counts from the first-pass mean are excluded)
2. Subtract the dark reference from each channel (dark-corrected signal)
3. Compute relative transmittance: `corrected_sample / (colorlessRef - darkRef)` per channel
4. Apply 3-point spectral smoothing
5. Classify color using the multi-anchor classifier (see below)

**Multi-anchor classifier (`classifyColor`):**

Builds a reference transmittance curve for each of the 5 color anchors (colorless, pale yellow, yellow, dark yellow, pink), then picks the closest match via weighted euclidean distance. Weights bias the comparison toward the red/yellow bands (F6–F8) because that's where the clinically-relevant color changes live. Sanity checks reject samples with average transmittance below 0.03 (signal too weak — LED or tube positioning issue) or above 1.45 (brighter than the colorless reference — tube misalignment).

A guard prevents false Pink classifications: Pink can only win if the computed "redness score" (red-to-green and red-to-blue transmittance ratios) exceeds 1.10. Without this guard, dim noisy samples can accidentally land closer to the pink anchor by raw euclidean distance.

Each classification returns a confidence score (0–0.98) based on how close the winning anchor is. Debug output on Serial includes all 5 distances and the redness score.

**Color outputs:** Colorless, Pale Yellow, Yellow, Dark Yellow, Pink. (The classifier can also fall through to "Unknown" on sanity-check failures, and `isAbnormalColorLabel` also recognizes legacy labels like Amber, Brown, Red, Dark Amber, Pink/Red from earlier iterations.)

**Abnormal colors** (trigger an ABNORMAL COLOR alert): Dark Yellow, Pink, Red, Pink/Red, Amber, Dark Amber, Brown

The color sensor polls every 3 seconds (`COLOR_POLL_INTERVAL`). The weight pipeline runs independently — if the AS7341 isn't connected or fails to initialize, the rest of the device still works.

### Display and UI

The RA8875 is a 480×272 resistive touchscreen. The screen is divided into two panels:

- **Left panel (0–299 px):** volume display box, color/opacity section, alert box, DISMISS button, RESET button
- **Right panel (300–479 px):** scrollable log (Alert Log or Output Log), "Last add" timestamp, live clock, VIEW toggle button

**Log views:** The VIEW button on the bottom-right toggles between:
- **Alert Log** — last 5 alerts (DROP, LEAK, ABNORMAL COLOR, NEAR FULL, BAG FULL, NO OUTPUT) with timestamps and volumes
- **Output Log** — last 5 patient void events with `+N mL` and running total

**Touch handling:** Touch coordinates are mapped from raw RA8875 ADC values (76–961, 127–900) to screen pixels (0–479, 0–271). Touch has a 350ms debounce. Critically, `tft.touchRead()` must be called every pass even if debounce blocks action — failure to do so leaves the hardware register set and causes buttons to appear stuck after first use.

### Alert System

Alerts are prioritized (highest to lowest):

| Priority | Alert | Trigger | Color | Tone |
|---|---|---|---|---|
| 1 | DROP DETECTED | Weight drops from >50g to <20g in one stable reading | Red | 2000 Hz |
| 2 | LEAK DETECTED | Volume drops 1–200 mL for 3 consecutive 2-second checks | Yellow | 1200 Hz |
| 2b | NO OUTPUT - CHECK | Bag reads below 20g continuously for 30 minutes | Yellow | 1000 Hz |
| 3 | ABNORMAL COLOR | Color classified as Dark Yellow, Pink, Red, etc. | Magenta | 1500 Hz |
| 4 | BAG FULL | Volume ≥ 2900 mL | Red | 2500 Hz |
| 5 | NEAR FULL | Volume ≥ 2500 mL | Yellow | 1800 Hz |

A higher-priority alert can supersede a lower-priority one (e.g., a LEAK fires even if BAG FULL was already active — this matters for the 2900 mL leak test). DROP and LEAK cannot be superseded except by each other, in priority order.

All alerts require acknowledgement via the **DISMISS** button. Alerts repeat as audible beeps every 500ms until dismissed. **RESET** (red button) clears all alerts, re-tares the scale, zeroes all state, and is intended for when the nurse empties or replaces the bag.

### Time Sync

The Arduino does not have a real-time clock (RTC) module. Time is synced by sending a Unix timestamp over Serial from `send_time.py` running on a connected laptop. The Arduino reads any all-numeric line from Serial and sets its internal clock using `TimeLib`.

The Python sync script (`send_time.py`) lives in this repo. It looks like:
```python
import serial
import time
from datetime import datetime

# CHANGE THIS to your actual Arduino port
ser = serial.Serial('COM9', 9600, timeout=1)

time.sleep(2)  # allow Arduino to reset

while True:
    now = int(datetime.now().timestamp())  # Unix time
    ser.write(f"{now}\n".encode())
    print(f"Sent: {now}")
    time.sleep(1)
```

To use: open a terminal, `cd` to the folder containing `send_time.py`, and run `python send_time.py` (requires Python and `pyserial`: `pip install pyserial`). The script sends Unix timestamps every second; the device's clock and timestamps in the logs will update accordingly.

**Notes:**
- The Arduino IDE Serial Monitor must be **closed** when running the Python script — only one process can hold the serial port at a time
- `TimeLib` (from `Time.zip` in the bundled libraries) must be installed
- `TIMEZONE_OFFSET_SECONDS` at the top of the combined sketch controls the display timezone (default: EDT, `-4 * 3600`; for EST use `-5 * 3600`)

---

## 7. Getting Started

### Arduino Library Dependencies

Everything needed is bundled in `libraries.zip`. Extract it, then extract each individual zip inside and drop the resulting folders into your Arduino libraries folder (usually `Documents/Arduino/libraries/`):

| Library | Bundled Zip | Notes |
|---|---|---|
| `HX711_ADC` by Olav Kallhovd | `HX711_ADC.zip` | Do NOT use the standard `HX711` by Rob Tillaart — different API |
| `Adafruit_RA8875` | `Adafruit_RA8875.zip` | Touchscreen driver |
| `Adafruit_GFX_Library` | `Adafruit_GFX_Library.zip` | Required by RA8875 |
| `Adafruit_AS7341` | `Adafruit_AS7341.zip` | Color sensor |
| `TimeLib` | `Time.zip` | Real-time clock support (Paul Stoffregen's Time library) |

You cannot drag the parent `libraries/` folder into the Arduino libraries folder — each individual zip must be extracted separately and its resulting folder placed directly in `Documents/Arduino/libraries/`.

### Flashing the Firmware

1. Open `urine_monitor_combined_iteration8.ino` in Arduino IDE
2. Select **Board:** Arduino Mega 2560
3. Select the correct **Port**
4. Click **Upload**
5. Open Serial Monitor at **9600 baud**

On startup you will see:
```
Remove weight for tare...
Scale ready.
AS7341 ready — hardcoded calibration applied
  Gain level: 7
  LED current mA: 4
  Measurement enabled immediately (no 'c' needed)
```

If `RA8875 not found` or `Tare failed` appears, the sketch halts — check connections before continuing. If `AS7341 not found` appears, the weight pipeline still works but color classification won't run — check the I2C wiring (pins 20/21) and that the breakout is powered.

### Load Cell Calibration

Follow the full procedure in `LoadCellCalibrationInstructions.pdf`. The workflow is:

1. Extract `HX711_ADC.zip` from `libraries.zip` into your Arduino libraries folder
2. In Arduino IDE: `File → Examples → HX711_ADC → Calibration`
3. Change `Serial.begin(57600)` to `Serial.begin(9600)` (line 37 of the example)
4. Confirm `uint8_t dataPin = 4; uint8_t clockPin = 5;` matches our wiring
5. Flash the calibration example and open Serial Monitor
6. Follow the prompts: remove weight, tare, then place a known weight and enter its mass
7. Copy the printed calibration factor
8. In the combined sketch, update: `float calibrationFactor = <your value>;` (currently `210.036758`)

Re-run this whenever:
- The load cell hardware is replaced or re-mounted
- Readings drift significantly from known weights
- A fresh physical build is assembled

> **Critical:** Bad solder connections between the load cell wires and the HX711 breakout are the #1 cause of noisy or incorrect readings. Use soldered connections — do not rely on breadboard contacts or tape.

### Color Sensor Calibration

Iteration 8 replaced the old interactive calibration with a **hardcoded calibration block** modeled after the load cell calibration flow. You run a dedicated calibration sketch once, copy the output, and paste it into the combined sketch. After that, the color sensor works immediately on boot — no re-calibration needed per power cycle.

Follow the full procedure in `ColorSensorCalibrationInstructions.pdf`. The workflow is:

1. Prepare 6 reference samples:
   - Dark (sensor covered / blocked optical path)
   - Colorless (distilled water or empty clear tube)
   - Pale Yellow (very dilute yellow food coloring)
   - Yellow (medium yellow dilution)
   - Dark Yellow (concentrated yellow)
   - Pink / Red (diluted red food coloring)
2. Flash `AS7341_calibration.ino` and open Serial Monitor at **115200 baud**
3. Send `c` to start the 6-step calibration flow
4. For each step, position the sample, wait a moment, then send `y` to capture
5. After step 6, send `p` — this prints a paste-ready C block starting with `// ======= BEGIN COLOR SENSOR CALIBRATION VALUES =======`
6. Copy everything between (and including) the BEGIN and END markers
7. In the combined sketch, Find (Ctrl+F) `BEGIN COLOR SENSOR CALIBRATION VALUES` and replace the existing block with your new one
8. Re-flash the combined sketch

Re-run this whenever:
- The AS7341 breakout is remounted or the tube holder is redesigned
- The LED is replaced or its distance to the sample changes
- The sensor moves to a room with very different ambient lighting
- The device is rebuilt with a new AS7341 unit (every breakout has slightly different per-channel sensitivity)
- Verification on a known sample starts failing

**LED current tuning:** Use `+` and `-` commands over Serial during calibration to raise or lower the LED drive current (`ledMA`). Start at 4 mA and raise until the sensor reads clean values without saturation. If channels hit >60,000 counts, the signal is saturated — reduce current or increase distance slightly.

### Time Sync Setup

1. Install Python 3 and pyserial: `pip install pyserial`
2. Edit `send_time.py` and change `'COM9'` to your Arduino's actual port (e.g., `COM3` on Windows, `/dev/ttyUSB0` on Linux, `/dev/cu.usbmodemXXXX` on macOS)
3. Close the Arduino IDE Serial Monitor (only one process can hold the port)
4. Run: `python send_time.py`
5. The clock on the device display and timestamps in logs will update to the current time

If the script isn't running, the display shows `--:--:--` for all times, but all other functions work normally.

---

## 8. Key Tunable Constants

These are the most likely values to need adjustment during testing. They are all near the top of the combined sketch:

| Constant | Current Value | Description |
|---|---|---|
| `calibrationFactor` | `210.036758` | Load cell calibration — hardware-specific, must be re-run if load cell changes |
| `GRAMS_TO_ML` | `1.0` | Density conversion. Urine is 1.005–1.035 g/mL; 1.0 is a safe approximation |
| `NEAR_FULL_ML` | `2500` | Volume at which NEAR FULL alert fires |
| `FULL_ML` | `2900` | Volume at which BAG FULL alert fires |
| `ADD_EVENT_THRESHOLD_ML` | `25` | Minimum mL increase to log an output event (filters scale noise and drift) |
| `ADD_EVENT_COOLDOWN_MS` | `90000` | Milliseconds between logged output events (prevents one void from generating many entries) |
| `THRESHOLD` | `5.0` | Max gram-to-gram variation allowed before stable count resets |
| `STABLE_COUNT_REQUIRED` | `5` | Consecutive stable readings required before a weight is accepted |
| `DOWNWARD_COUNT_REQUIRED` | `8` | Consecutive decreasing readings required before display weight decreases |
| `DROP_TO_ZERO_THRESHOLD` | `20.0` | If displayed weight was >50g and new stable weight is below this, a DROP event fires |
| `LEAK_CHECK_INTERVAL` | `2000` | ms between leak check snapshots |
| `LEAK_DROP_THRESHOLD` | `200` | mL drop per interval considered a potential leak (above this is treated as intentional handling/dump) |
| `LEAK_MIN_DROP_THRESHOLD` | `1` | mL drop minimum to count toward leak trend |
| `LEAK_TREND_REQUIRED` | `3` | Consecutive leak-sized drops before LEAK alert fires (~6s total) |
| `LEAK_REARM_STABLE_INTERVALS` | `3` | Consecutive no-drop intervals needed to re-arm leak detection after dismissal |
| `EXTENDED_ZERO_TIMEOUT_MS` | `1800000` (30 min) | How long the bag must read near-empty before NO OUTPUT alert fires |
| `EXTENDED_ZERO_WEIGHT_G` | `20.0` | Weight threshold for "near-empty" |
| `COLOR_POLL_INTERVAL` | `3000` | ms between color sensor readings |
| `TIMEZONE_OFFSET_SECONDS` | `-14400` (EDT, -4h) | Change for your timezone (EST = -18000) |

---

## 9. Alert Logic Reference

### Dismissal and Reset behavior

- **DISMISS** — acknowledges the current alert and clears it from the screen. The per-alert acknowledged flag is set (e.g., `nearFullAcknowledged = true`). If the condition persists (e.g., bag is still FULL), the alert will NOT re-fire until the condition clears and returns
- **RESET** — intended for bag change / nurse handoff. Clears all alerts, re-tares the scale with `scale.tare()`, resets all volume tracking, clears all logs, and resets all acknowledged flags. Use this whenever the bag is emptied or replaced

### Leak detection

Leak detection runs off `currentWeight` (the undamped fresh stable reading), **not** `displayedWeight` (which has the downward-trend damping and would hide slow drains by quantizing the signal to 0 between jumps). Every `LEAK_CHECK_INTERVAL` (2000 ms), the current volume is compared to the last snapshot:

- **Drop between 1 and 200 mL per 2s** → trend counter increments
- **Drop greater than 200 mL** → treated as intentional handling / dump, counter resets
- **3 consecutive qualifying drops (~6s total)** → LEAK alert fires

LEAK can supersede lower-priority alerts (FULL, NEAR FULL, ABNORMAL COLOR, NO OUTPUT) — this is essential for the 2900 mL full-bag leak test, since FULL would otherwise block the leak alert.

After a LEAK is dismissed, detection stays disarmed until 3 consecutive no-drop intervals pass (`LEAK_REARM_STABLE_INTERVALS`). This lets a second leak event in the same session still alert. Any volume add event also resets the trend counter and re-arms leak detection.

### Extended-zero detection (silent leak / valve-left-open)

Some silent-leak scenarios leave no weight signal to detect from — e.g., the small urimeter drain valve was open before the bag was installed, so urine leaks out without ever registering. Extended-zero detection catches this with a time-based check: if the bag reads below `EXTENDED_ZERO_WEIGHT_G` (20 g) continuously for `EXTENDED_ZERO_TIMEOUT_MS` (30 minutes), the NO OUTPUT - CHECK alert fires.

This also catches true clinical anuria, which is equally concerning. The alert text tells the nurse what to check: urimeter valve, main drain valve, disconnected tubing, or patient status.

For bench testing, temporarily reduce `EXTENDED_ZERO_TIMEOUT_MS` to `60000UL` (1 min) so the alert fires quickly, then restore to 30 minutes for clinical use.

---

## 10. Serial Command Reference

Iteration 8 removed the interactive color calibration flow. Commands below are for runtime diagnostics and time sync. To re-calibrate the color sensor, use `AS7341_calibration.ino` (separate sketch — see [Color Sensor Calibration](#color-sensor-calibration)).

**Combined sketch (9600 baud):**

| Command | Action |
|---|---|
| `p` | Print currently-loaded color calibration values (diagnostic) |
| `s` | Toggle color measurement on / off |
| `+` | Increase LED current by 2 mA (max 150 mA) |
| `-` | Decrease LED current by 2 mA (min 2 mA) |
| `<unix timestamp>\n` | Sync the real-time clock (sent by `send_time.py`) |

**Standalone AS7341_calibration.ino (115200 baud):**

| Command | Action |
|---|---|
| `c` | Start / restart full 6-step calibration |
| `y` | Confirm current calibration step |
| `p` | Print paste-ready C code (when all 6 references done) |
| `?` | Show calibration status |
| `+` / `-` | Nudge LED current by 2 mA |
| `h` | Print help menu |

---

## 11. Testing Plan

### Load Cell (Quantitative)

**Accuracy test:**
- Attach the Foley bag with zero fluid
- Add known volumes of saline (0.9% NaCl, density ≈ 1.005 g/mL) using a calibrated lab scale
- Record device reading vs. true volume at 8 volume levels, 3 trials each (24 total)
- Run a paired t-test; device passes if p > 0.05 (no significant difference from true values)
- Target accuracy: ±10 mL

**Leak detection test:**
- Fill bag to 2900 mL
- Open either drain valve and let it leak at a natural rate
- LEAK alert should fire within ~6–10 seconds; alert log should record the event
- Expected Serial output pattern:
  ```
  Leak check: prev=2900 mL  now=2880 mL  drop=20 mL  trend=1/3
  Leak check: prev=2880 mL  now=2862 mL  drop=18 mL  trend=2/3
  Leak check: prev=2862 mL  now=2845 mL  drop=17 mL  trend=3/3
  *** LEAK ALERT FIRED ***
  ```

**Extended-zero test:**
- Temporarily set `EXTENDED_ZERO_TIMEOUT_MS` to `60000UL` (1 min)
- Leave empty bag on the device
- NO OUTPUT - CHECK alert should fire ~1 minute after boot

**Stability / drift test:**
- Place a fixed known load on the device for 24+ continuous hours
- Record readings at regular intervals
- Observe whether calibrationFactor drift causes systematic error over time

### Color Sensor (Qualitative)

**Classification test:**
- Prepare solutions matched to the 5 calibration anchors, plus a red blood-like solution, and a murky white solution
- Test each solution 3 times using the hardcoded classifier
- Record device output (color label) vs. known condition (normal / abnormal)
- Device passes if correct classification rate > 95%

**Verification after re-calibration:**
- After every color sensor re-calibration, verify on known samples:
  - Colorless water → "Colorless"
  - Yellow sample → "Yellow"
  - Pink sample → "Pink" + ABNORMAL COLOR alert
  - Dark Yellow sample → "Dark Yellow" + ABNORMAL COLOR alert
- If any fail, the calibration didn't generalize — re-run with better-differentiated samples

### Usability / Human Factors (ANSI/AAMI HE75)

- Simulate a bedside environment
- Ask representative users (ideally nurses or nursing students) to install the bag and calibrate the device with minimal instruction
- Record: first-attempt bag installation success rate (target ≥ 90%), setup time (target < 2 min), calibration success without assistance (target ≥ 95%), correct alert interpretation (target ≥ 90%)

---

## 12. Known Issues and Limitations

- **Color calibration values live in source code, not EEPROM.** Re-flashing the sketch preserves the current calibration since it's baked into the source, but any change to the physical setup still requires re-running `AS7341_calibration.ino` and pasting new values. Storing the values in EEPROM would eliminate the paste step.

- **No RTC module.** Time relies on `send_time.py` over USB. If the laptop is disconnected, the clock runs on `millis()` and will drift. An RTC module (e.g., DS3231) would make the device standalone.

- **Color sensor performance degrades with tube misalignment.** The AS7341 is highly sensitive to the exact position of the sample tube. If the tube shifts between calibration and measurement, readings will be unreliable. A mechanical fixture to lock tube position is needed for consistent results.

- **Load cell sensitivity to ambient vibration.** The current mounting setup is susceptible to mechanical disturbance (bumping the table, someone brushing the bag). The stability filter (`STABLE_COUNT_REQUIRED = 5`) mitigates this but adds latency to accepted readings.

- **Single-file sketch size.** The combined `.ino` is ~1,770 lines. As features are added, consider splitting into multiple `.ino` files within the same sketch folder (Arduino IDE compiles all `.ino` files in a folder together).

- **No wireless transmission.** Data is local only. Hospital EMR integration is a future milestone, as is Bluetooth/Wi-Fi data push.

- **Urine bag visibility constraint.** The open-bag design (bag hangs exposed on the J-hook) was chosen specifically so nurses can still visually inspect the bag. A closed enclosure would reduce this visibility and was rejected based on nurse feedback.

---

## 13. Design Decisions and Trade-offs

| Decision | Choice Made | Why | What was rejected |
|---|---|---|---|
| Volume measurement method | Load cell (weight-based) | Compatible with any standard Foley bag; no fluid contact; reusable | Optical level sensing (requires clear chamber, hard to clean); ultrasound (complex integration) |
| Color sensing approach | AS7341 spectral sensor, inline tube sample | 10-channel spectral data enables nuanced color classification | Simple RGB sensor (insufficient spectral resolution); camera-based vision |
| Color sensor breakout | Adafruit AS7341 breakout | Plug-and-play I2C; adequate clearance for manual wiring | Bare AMS-Osram AS7341 chip (too small, requires reflow soldering) |
| Bag enclosure | Open (bag visible) | Nurses need to see the bag for visual inspection of sediment | Closed chamber (rejected by nurses during consultation) |
| Display | RA8875 480×272 resistive touchscreen | Large enough to read from doorway; touch allows DISMISS/RESET | LED indicator only (insufficient information density) |
| Wireless | None (this iteration) | Reduces risk surface; simpler prototype | Bluetooth / Wi-Fi (deferred to future work) |
| Color calibration persistence | Hardcoded paste-block in source | Survives power cycles; matches load cell calibration pattern; no runtime prompts | Interactive per-boot calibration (iteration ≤6 approach — too fragile for clinical use); EEPROM persistence (deferred — paste approach was faster to implement reliably) |
| Color classifier | 5-anchor weighted spectral distance with redness guard | Only produces labels the device has evidence for; weighted bands bias to clinically-relevant colors | Rule-based transmittance heuristic (iteration ≤6 — too many false categorical claims); kNN / ML approach (deferred — requires dataset) |
| Leak detection signal | `currentWeight` (undamped stable reading) | Slow drains stay visible; short detection latency | `displayedWeight` (early attempt — damping quantized signal to 0, masked leaks) |
| Leak drop range | 1–200 mL per 2s | Covers trickle up to 6 L/min flow; everything above is "nurse-initiated" | 2–12 mL per 3s (iteration ≤6 — full-bag drains easily exceeded 12 mL/interval and reset the trend every check) |
| Silent-leak detection | Time-based (30 min below 20g) | Catches urimeter-valve-open scenario where weight never accumulates | Weight-slope-only (can't detect what was never there) |

---

## 14. Future Work

Roughly in priority order for a continuation team:

1. **EEPROM persistence for color calibration** — save the 6 reference arrays to EEPROM so re-calibration doesn't require re-flashing. The `EEPROM.h` library is built into Arduino. Compared to the current paste-block approach, this is a convenience win (recalibration without source code edits) but not a correctness win.

2. **RTC module integration** — add a DS3231 or similar I2C RTC so time is maintained without a laptop connection.

3. **Physical tube fixture for color sensor** — design or 3D-print a clamping mount that locks the sample tube at a fixed, repeatable position relative to the AS7341 LED and detector.

4. **Wireless data transmission** — add an ESP8266/ESP32 or similar module to push timestamped volume and color data to a local network endpoint. Be careful about HIPAA considerations around where data lands.

5. **Flow rate calculation** — the current output log shows total volume added per event. Computing and displaying mL/hr would be clinically useful for detecting oliguria in real time.

6. **Improved color classifier** — the current 5-anchor classifier works well for the 5 calibrated colors, but a kNN classifier trained on real urine samples (or a lookup table over a finer hue grid) would be more robust to samples that sit between the anchors.

7. **Drift testing** — formally measure load cell calibration drift over 24+ hours of continuous loading and quantify the error over a full bag cycle.

8. **Waterproofing / enclosure** — the current prototype is not splash-resistant. A final housing should be IP-rated and constructed from materials that can be wiped down with hospital-grade disinfectant.

9. **EMR integration** — long-term goal is pushing data to Epic or a similar system via HL7/FHIR API. This requires hospital IT involvement and regulatory consideration (FDA Class II 510(k) pathway).

---

## 15. Related Resources

- **BME Final Capstone Report** — full clinical background, concept selection, Pugh matrix, engineering standards, and testing plan
- **Load Cell Calibration Instructions** — `LoadCellCalibrationInstructions.pdf` — step-by-step photos of correct load cell mounting and HX711 wiring, with the HX711 example sketch walkthrough
- **Color Sensor Calibration Instructions** — `ColorSensorCalibrationInstructions.pdf` — step-by-step guide for capturing the 6 reference spectra with `AS7341_calibration.ino` and pasting the output into the combined sketch
- **Bundled Libraries** — `libraries.zip` — all Arduino libraries needed (HX711_ADC, RA8875, GFX, AS7341, TimeLib). Use these bundled versions — some of the Library Manager versions have different APIs

### External References

- Adafruit RA8875 guide: https://learn.adafruit.com/adafruit-ra8875-driver-board
- Adafruit AS7341 guide: https://learn.adafruit.com/adafruit-as7341-10-channel-light-color-sensor-breakout
- HX711_ADC library: https://github.com/olkal/HX711_ADC
- TimeLib library: https://github.com/PaulStoffregen/Time
- Accuryn Monitoring System (commercial reference): https://accuryn.com
- Özer et al., 2023 — Microcontroller-based urine output monitoring system: https://doi.org/10.1177/09596518221145795
