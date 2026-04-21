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

The device hangs a standard Foley drainage bag from an internal load cell to continuously weigh the bag and convert grams to milliliters. A separate spectral color sensor (AS7341) is inserted inline in the tubing to detect qualitative abnormalities in urine color (blood, dark amber, pink, brown, etc.). Readings are shown on a touchscreen mounted on the device enclosure, with audible and visual alerts for abnormal conditions.

### What came before this code

Previous OSU capstone teams (2017, 2020, 2021) each attempted similar devices. The 2017 project used a hanging load cell but suffered from calibration drift. The 2020 project used optical fluid-level sensing inside a clear chamber — effective but hard to clean. The 2021 project used capacitance sensing with Bluetooth, achieving near-100% bench precision but was difficult to sterilize and not compatible with standard Foley bags.

This iteration combines weight-based volume measurement with spectral color sensing, keeping the standard bag intact and external to the electronics — avoiding the sterilization and compatibility issues of prior designs.

Commercial devices like the Accuryn® (Potrero Medical) and BD Sensica™ solve similar problems but cost far more, require proprietary catheters, and are not accessible for most hospitals. This project is positioned as a low-cost, retrofit-compatible alternative.

---

## 2. Clinical Context

ICU nurses need to:
- Track urine output volume continuously (not just hourly)
- Detect sudden drops (bag accidentally emptied or disconnected)
- Detect slow leaks
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
│                   Arduino Mega 2560                  |
│                                                      │
│  Load Cell → HX711 ADC → weight filtering → volume   │
│                                                      │
│  AS7341 color sensor → spectral calibration          │
│                       → color classification         │
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

The device runs a single combined `.ino` sketch (`urine_monitor_combined_iterationN.ino`) on the Arduino Mega 2560. There is no wireless communication or cloud integration at this stage — all data is local to the device and displayed on the touchscreen.

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
├── README.md                              ← You are here
│
├── urine_monitor_combined_iterationN.ino  ← Primary combined firmware (use latest N)
│
├── Color_Sensor.ino                       ← Standalone color sensor development sketch
│                                            (used to develop & validate the color pipeline
│                                             independently before merging into combined)
│
├── HX_calibration.ino                     ← Standalone load cell calibration utility
│                                            (run this first on a fresh load cell to get
│                                             calibrationFactor; see calibration section below)
│
├── LoadCellCalibrationInstructions.pdf    ← Step-by-step calibration guide with photos
│
├── HX711_ADC.zip                          ← Bogde HX711_ADC library source (use this,
│                                            not the standard HX711 library — different API)
│
└── HX711_MERGED.zip                       ← Merged/older library archive (reference only)
```

> **Naming convention:** The combined main sketch is versioned by iteration number (e.g., `iteration4`, `iteration6`). Always work from the highest-numbered file. Do not edit older iteration files — keep them as a history reference.

---

## 6. Software Architecture

### Combined Main Sketch

The combined sketch (`urine_monitor_combined_iterationN.ino`) is the only file that needs to be flashed for normal operation. It integrates:

- Load cell weight reading and filtering
- Color sensor calibration and measurement
- RA8875 touchscreen UI with touch handling
- Alert system with audible tones
- Output event log and alert event log
- Real-time clock (synced via Serial from Python)
- Leak detection
- Hourly output logging

The `loop()` runs the following in order each cycle (with a 50ms delay):

```
handleSerialInput()   → time sync + color commands
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

**Key insight for future debugging:** The `calibrationFactor` constant (currently `212.46006`) is load-cell-specific. If you replace or re-mount the load cell hardware, you must re-run `HX_calibration.ino` and update this value.

### Color Sensor Pipeline

The AS7341 measures 10 channels of spectral light (415–680 nm + Clear + NIR). The pipeline works as follows:

**Calibration (done once per session):**
1. **Dark reference** — sensor covered, LED on. Captures the sensor's dark current/noise floor
2. **Colorless reference** — clear water or empty tube in position. This is the baseline "no urine" signal
3. **Yellow reference** — yellow reference sample. Used to anchor the classifier

**Per-reading measurement:**
1. Take 12 averaged samples with outlier rejection (readings more than 2500 counts from the first-pass mean are excluded)
2. Subtract the dark reference from each channel (dark-corrected signal)
3. Compute relative transmittance: `corrected_sample / (colorlessRef - darkRef)` per channel
4. Apply 3-point spectral smoothing
5. Classify color using `classifyColor()` — a rule-based spectral classifier using blue/green/red band averages and average transmittance thresholds

**Color outputs:** Colorless, Pale Yellow, Light Yellow, Yellow, Dark Yellow, Amber, Dark Amber, Brown, Pink, Pink/Red, Red

**Abnormal colors** (trigger an alert): Amber, Brown, Pink, Red, Pink/Red, Dark Amber

The color sensor polls every 3 seconds (`COLOR_POLL_INTERVAL`). If the sensor isn't started or calibration hasn't been run, polling is silently skipped — the weight pipeline still runs normally.

> **Note on the standalone `Color_Sensor.ino`:** This was the development version of the color pipeline. It uses a more elaborate multi-anchor classification approach with up to 7 calibration steps and an SNV normalization pass. The combined sketch uses a simpler 3-reference approach (dark, colorless, yellow) that proved more practical during clinical prototyping. If color accuracy needs improvement, the multi-anchor approach from `Color_Sensor.ino` can be ported back into the combined sketch.

### Display and UI

The RA8875 is a 480×272 resistive touchscreen. The screen is divided into two panels:

- **Left panel (0–299 px):** volume display box, color/opacity section, alert box, DISMISS button, RESET button
- **Right panel (300–479 px):** scrollable log (Alert Log or Output Log), "Last add" timestamp, live clock, VIEW toggle button

**Log views:** The VIEW button on the bottom-right toggles between:
- **Alert Log** — last 5 alerts (DROP, LEAK, ABNORMAL COLOR, NEAR FULL, BAG FULL) with timestamps and volumes
- **Output Log** — last 5 patient void events with `+N mL` and running total

**Touch handling:** Touch coordinates are mapped from raw RA8875 ADC values (76–961, 127–900) to screen pixels (0–479, 0–271). Touch has a 350ms debounce. Critically, `tft.touchRead()` must be called every pass even if debounce blocks action — failure to do so leaves the hardware register set and causes buttons to appear stuck after first use.

### Alert System

Alerts are prioritized (highest to lowest):

| Priority | Alert | Trigger | Color | Tone |
|---|---|---|---|---|
| 1 | DROP DETECTED | Weight drops from >50g to <20g in one reading | Red | 2000 Hz |
| 2 | LEAK DETECTED | Volume drops 2–12 mL for 3 consecutive 3-second checks | Yellow | 1200 Hz |
| 3 | ABNORMAL COLOR | Color classified as Amber, Brown, Pink, Red, etc. | Magenta | 1500 Hz |
| 4 | BAG FULL | Volume ≥ 2900 mL | Red | 2500 Hz |
| 5 | NEAR FULL | Volume ≥ 2500 mL | Yellow | 1800 Hz |

All alerts require acknowledgement via the **DISMISS** button. Alerts repeat as audible beeps every 500ms until dismissed. **RESET** (red button) clears all alerts, re-tares the scale, zeroes all state, and is intended for when the nurse empties or replaces the bag.

### Time Sync

The Arduino does not have a real-time clock (RTC) module. Time is synced by sending a Unix timestamp over Serial from a Python script running on a connected laptop. The Arduino reads any all-numeric line from Serial and sets its internal clock using `TimeLib`.

Our simple Python sync script can be found in this repository, it looks like:
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

By opening a terminal window and changing directory to where `send_time.py` is located, run the command `python send_time.py` (make sure python is installed on your computer). The script will begin sending UNIX time to the Arduino and the alerts/output log section of the screen should update to include timestamps.

Note: serial monitor must for the Arduino must be available when the Python script is ran (cannot have serial monitor open in Arduino IDE at that time). You will also need TimeLib installed in your libraries folder (Time.zip in this repository)

---

## 7. Getting Started

### Arduino Library Dependencies

Install all of the following through the Arduino Library Manager or by extracting the provided zips:

| Library | Source | Notes |
|---|---|---|
| `HX711_ADC` by Olav Kallhovd | **Use the version in `HX711_ADC.zip`** | Do NOT use the standard `HX711` by Rob Tillaart — different API |
| `Adafruit_RA8875` | Arduino Library Manager | Touchscreen driver |
| `Adafruit_GFX` | Arduino Library Manager | Required by RA8875 |
| `Adafruit_AS7341` | Arduino Library Manager | Color sensor |
| `TimeLib` | Arduino Library Manager (search "Time by PaulStoffregen") | Real-time clock support |

### Flashing the Firmware

1. Open the latest `urine_monitor_combined_iterationN.ino` in Arduino IDE
2. Select **Board:** Arduino Mega 2560
3. Select the correct **Port**
4. Click **Upload**
5. Open Serial Monitor at **9600 baud**

On startup you will see:
```
Remove weight for tare...
Scale ready.
AS7341 ready
Send 'c' to begin color calibration
```

If `RA8875 not found` or `Tare failed` appears, the sketch halts — check connections before continuing.

### Load Cell Calibration

Run `HX_calibration.ino` (separate sketch, not the combined sketch) whenever:
- The load cell hardware is replaced or re-mounted
- Readings drift significantly from known weights
- A fresh physical build is assembled

**Steps:**
1. Flash `HX_calibration.ino`
2. Open Serial Monitor at 9600 baud
3. Remove all weight from the load cell, press Enter
4. Place a known weight (e.g., 500g), type the weight in grams, press Enter
5. Copy the printed `SCALE` value
6. In the combined sketch, update: `float calibrationFactor = <your value>;`

See `LoadCellCalibrationInstructions.pdf` for photos of the correct mounting setup (one end fixed, free end deflects under load).

> **Critical:** Bad solder connections between the load cell wires and the HX711 breakout are the #1 cause of noisy or incorrect readings. Use soldered connections — do not rely on breadboard contacts or tape.

### Color Sensor Calibration

Color calibration is done every time the Arduino is powered on. The calibration state is not persisted to EEPROM (a potential future improvement).

Send serial commands to step through calibration:

1. Send `c` — begins calibration sequence
2. **Step 1 — Dark:** Block the sensor's optical path completely (cover the tube / remove sample), then send `y`
3. **Step 2 — Colorless:** Insert a tube of clear water or leave empty (representing colorless urine), then send `y`. The auto-gain routine runs first to pick the best gain setting
4. **Step 3 — Yellow:** Insert a reference yellow sample (a prepared diluted food dye or reference solution matched to hydration scale color 4), then send `y`
5. Send `s` — activates continuous measurement mode

The color sensor then polls every 3 seconds and updates the screen automatically.

**LED current tuning:** Use `+` and `-` commands over Serial to raise or lower the LED drive current (`ledMA`). Start at 4 mA and raise until the sensor reads clean values (not saturated). If channels hit >60,000 counts, the signal is saturated — reduce current or increase distance slightly.

### Time Sync Setup

Run the Python time sync script while the Arduino is connected via USB. The clock on the display will update immediately. If the Python script is not run, the display shows `--:--:--` for all times, but all other functions work normally.

---

## 8. Key Tunable Constants

These are the most likely values to need adjustment during testing. They are all near the top of the combined sketch:

| Constant | Current Value | Description |
|---|---|---|
| `calibrationFactor` | `212.46006` | Load cell calibration — hardware-specific, must be re-run if load cell changes |
| `GRAMS_TO_ML` | `1.0` | Density conversion. Urine is 1.005–1.035 g/mL; 1.0 is a safe approximation |
| `NEAR_FULL_ML` | `2500` | Volume at which NEAR FULL alert fires |
| `FULL_ML` | `2900` | Volume at which BAG FULL alert fires |
| `ADD_EVENT_THRESHOLD_ML` | `25` | Minimum mL increase to log an output event (filters scale noise and drift) |
| `ADD_EVENT_COOLDOWN_MS` | `90000` | Milliseconds between logged output events (prevents one void from generating many entries) |
| `THRESHOLD` | `5.0` | Max gram-to-gram variation allowed before stable count resets |
| `STABLE_COUNT_REQUIRED` | `5` | Consecutive stable readings required before a weight is accepted |
| `DOWNWARD_COUNT_REQUIRED` | `8` | Consecutive decreasing readings required before display weight decreases |
| `DROP_TO_ZERO_THRESHOLD` | `20.0` | If displayed weight was >50g and new stable weight is below this, a DROP event fires |
| `LEAK_CHECK_INTERVAL` | `3000` | ms between leak check snapshots |
| `LEAK_DROP_THRESHOLD` | `12` | mL drop per interval considered a potential leak (above this is treated as intentional removal) |
| `LEAK_MIN_DROP_THRESHOLD` | `2` | mL drop minimum to count toward leak trend |
| `LEAK_TREND_REQUIRED` | `3` | Consecutive leak-sized drops before LEAK alert fires |
| `COLOR_POLL_INTERVAL` | `3000` | ms between color sensor readings |
| `TIMEZONE_OFFSET_SECONDS` | `-14400` (EDT, -4h) | Change for your timezone (EST = -18000) |

---

## 9. Alert Logic Reference

### Dismissal and Reset behavior

- **DISMISS** — acknowledges the current alert and clears it from the screen. The per-alert acknowledged flag is set (e.g., `nearFullAcknowledged = true`). If the condition persists (e.g., bag is still FULL), the alert will NOT re-fire until the condition clears and returns
- **RESET** — intended for bag change / nurse handoff. Clears all alerts, re-zeros the scale with `scale.tare()`, resets all volume tracking, clears all logs, and resets all acknowledged flags. Use this whenever the bag is emptied or replaced

### Leak detection nuance

Leak detection uses a rolling trend counter. Every `LEAK_CHECK_INTERVAL` ms, the current volume is compared to the last snapshot. If the drop is between `LEAK_MIN_DROP_THRESHOLD` (2 mL) and `LEAK_DROP_THRESHOLD` (12 mL), the trend counter increments. Drops larger than 12 mL are assumed to be intentional handling and the counter resets. After 3 consecutive qualifying drops, `showLeakAlert()` fires. Any volume add event resets the trend counter and re-initializes the leak tracking baseline.

---

## 10. Serial Command Reference

| Command | Action |
|---|---|
| `c` | Start color calibration sequence (dark → colorless → yellow) |
| `y` | Confirm current calibration step |
| `s` | Start continuous color measurement (after calibration is complete) |
| `+` | Increase LED current by 2 mA (max 150 mA) |
| `-` | Decrease LED current by 2 mA (min 2 mA) |
| `<unix timestamp>\n` | Sync the real-time clock (sent by Python script) |

---

## 11. Testing Plan

### Load Cell (Quantitative)

**Accuracy test:**
- Attach the Foley bag with zero fluid
- Add known volumes of saline (0.9% NaCl, density ≈ 1.005 g/mL) using a calibrated lab scale
- Record device reading vs. true volume at 8 volume levels, 3 trials each (24 total)
- Run a paired t-test; device passes if p > 0.05 (no significant difference from true values)
- Target accuracy: ±10 mL

**Stability / drift test:**
- Place a fixed known load on the device for 24+ continuous hours
- Record readings at regular intervals
- Observe whether calibrationFactor drift causes systematic error over time

### Color Sensor (Qualitative)

**Classification test:**
- Prepare 10 solutions: 8 matched to the Armstrong/Stills 8-point hydration scale, plus a red blood-like solution, and a murky white solution
- Test each solution 3 times = 30 total readings
- Record device output (color label) vs. known condition (normal / abnormal)
- Device passes if correct classification rate > 95%

### Usability / Human Factors (ANSI/AAMI HE75)

- Simulate a bedside environment
- Ask representative users (ideally nurses or nursing students) to install the bag and calibrate the device with minimal instruction
- Record: first-attempt bag installation success rate (target ≥ 90%), setup time (target < 2 min), calibration success without assistance (target ≥ 95%), correct alert interpretation (target ≥ 90%)

---

## 12. Known Issues and Limitations

- **Calibration is not persisted.** Color calibration must be re-run every time the Arduino loses power. EEPROM storage of calibration values would make startup faster and more reliable for clinical use.

- **No RTC module.** Time relies on the Python sync script over USB. If the laptop is disconnected, the clock drifts using `millis()`. An RTC module (e.g., DS3231) would make the device standalone.

- **Color sensor performance degrades with tube misalignment.** The AS7341 is highly sensitive to the exact position of the sample tube. If the tube shifts between calibration and measurement, readings will be unreliable. A mechanical fixture to lock tube position is needed for consistent results.

- **Load cell sensitivity to ambient vibration.** The current mounting setup is susceptible to mechanical disturbance (bumping the table, someone brushing the bag). The stability filter (`STABLE_COUNT_REQUIRED = 5`) mitigates this but adds latency to accepted readings.

- **Single-file sketch size.** The combined `.ino` is large (~1,400+ lines). As features are added, consider splitting into multiple `.ino` files within the same sketch folder (Arduino IDE compiles all `.ino` files in a folder together).

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
| Color calibration references | 3-step (dark, colorless, yellow) | Practical for clinical use; consistent results | 7-step multi-anchor (developed in `Color_Sensor.ino` — more accurate but too complex for routine re-calibration) |

---

## 14. Future Work

Roughly in priority order for a continuation team:

1. **EEPROM persistence for color calibration** — save dark, colorless, and yellow references to EEPROM so calibration survives power cycles. The `EEPROM.h` library (built into Arduino) supports this.

2. **RTC module integration** — add a DS3231 or similar I2C RTC so time is maintained without a laptop connection.

3. **Physical tube fixture for color sensor** — design or 3D-print a clamping mount that locks the sample tube at a fixed, repeatable position relative to the AS7341 LED and detector.

4. **Wireless data transmission** — add an ESP8266/ESP32 or similar module to push timestamped volume and color data to a local network endpoint. Be careful about HIPAA considerations around where data lands.

5. **Flow rate calculation** — the current output log shows total volume added per event. Computing and displaying mL/hr would be clinically useful for detecting oliguria in real time.

6. **Improved color classifier** — the current `classifyColor()` function is a rule-based spectral heuristic. A small lookup-table or kNN classifier trained on real urine samples would be more robust. See `Color_Sensor.ino` for the more advanced multi-anchor approach.

7. **Drift testing** — formally measure load cell calibration drift over 24+ hours of continuous loading and quantify the error over a full bag cycle.

8. **Waterproofing / enclosure** — the current cardboard prototype is not splash-resistant. A final housing should be IP-rated and constructed from materials that can be wiped down with hospital-grade disinfectant.

9. **EMR integration** — long-term goal is pushing data to Epic or a similar system via HL7/FHIR API. This requires hospital IT involvement and regulatory consideration (FDA Class II 510(k) pathway).

---

## 15. Related Resources

- **BME Final Capstone Report** — `Urinary_Device_Written_Report.pdf` — full clinical background, concept selection, Pugh matrix, engineering standards, and testing plan
- **CSE Demo 1 Slides** — `Demo_1.pptx` — initial system architecture, load cell integration approach, and iteration 0/1 feature tracking
- **CSE Demo 2 Slides** — `User_Demo_2.pptx` — hardware integration status, AS7341 sensor switch rationale, regression test matrix, and iteration burn-ups
- **Load Cell Calibration Instructions** — `LoadCellCalibrationInstructions.pdf` — step-by-step photos of correct load cell mounting and HX711 wiring
- **HX711_ADC Library** — `HX711_ADC.zip` — use this version, not the standard HX711 library. The API is different (`scale.getData()` vs `scale.get_units()`)

### External References

- Adafruit RA8875 guide: https://learn.adafruit.com/adafruit-ra8875-driver-board
- Adafruit AS7341 guide: https://learn.adafruit.com/adafruit-as7341-10-channel-light-color-sensor-breakout
- HX711_ADC library: https://github.com/olkal/HX711_ADC
- TimeLib library: https://github.com/PaulStoffregen/Time
- Accuryn Monitoring System (commercial reference): https://accuryn.com
- Özer et al., 2023 — Microcontroller-based urine output monitoring system: https://doi.org/10.1177/09596518221145795
