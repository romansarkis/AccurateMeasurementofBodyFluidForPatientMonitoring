// ================================================================
// AS7341_calibration.ino
//
// Standalone calibration utility for the AS7341 color sensor.
// Analogous to HX_calibration.ino for the load cell: run this ONCE
// per physical setup, then paste the resulting values into
// urine_monitor_combined_iterationN.ino under the
// "HARDCODED COLOR CALIBRATION" section.
//
// Based on the multi-anchor calibration flow from Jim's standalone
// color sensor sketch (colorsensor.ino), simplified to focus on
// calibration-and-export only.
//
// QUICK START:
//   1. Upload this sketch to the Arduino Mega
//   2. Open Serial Monitor at 115200 baud
//   3. Send 'c' to start full calibration
//   4. Follow the prompts — you'll go through 6 reference samples:
//        dark, colorless, pale yellow, yellow, dark yellow, pink
//   5. When done, send 'p' to print a block of C code
//   6. Copy that block and paste it into the combined iteration
//      under "HARDCODED COLOR CALIBRATION"
//
// WIRING (matches combined iteration):
//   AS7341 VCC → 5V
//   AS7341 GND → GND
//   AS7341 SDA → Arduino Mega pin 20
//   AS7341 SCL → Arduino Mega pin 21
// ================================================================

#include <Adafruit_AS7341.h>

Adafruit_AS7341 as7341;

static const uint8_t NUM_SPECTRAL  = 8;
static const uint8_t NUM_CHANNELS  = 10;

static const int DARK_SAMPLES   = 20;
static const int REF_SAMPLES    = 20;
static const int SAMPLE_SAMPLES = 12;

uint16_t ledMA = 4;

enum ChIdx {
  CH_F1, CH_F2, CH_F3, CH_F4, CH_F5, CH_F6, CH_F7, CH_F8, CH_CLR, CH_NIR
};

static const char* const CH_NAMES[NUM_CHANNELS] = {
  "F1 415nm", "F2 445nm", "F3 480nm", "F4 515nm",
  "F5 555nm", "F6 590nm", "F7 630nm", "F8 680nm",
  "Clear", "Near IR"
};

// Reference storage — each holds the averaged raw readings for one
// calibration step. These are what get printed to Serial at the end.
uint16_t darkRef[NUM_CHANNELS];
uint16_t colorlessRef[NUM_CHANNELS];
uint16_t paleYellowRef[NUM_CHANNELS];
uint16_t yellowRef[NUM_CHANNELS];
uint16_t darkYellowRef[NUM_CHANNELS];
uint16_t pinkRef[NUM_CHANNELS];

bool darkCalibrated       = false;
bool colorlessCalibrated  = false;
bool paleYellowCalibrated = false;
bool yellowCalibrated     = false;
bool darkYellowCalibrated = false;
bool pinkCalibrated       = false;

enum CalibrationStep {
  CAL_IDLE,
  CAL_WAITING_FOR_DARK,
  CAL_WAITING_FOR_COLORLESS,
  CAL_WAITING_FOR_PALE_YELLOW,
  CAL_WAITING_FOR_YELLOW,
  CAL_WAITING_FOR_DARK_YELLOW,
  CAL_WAITING_FOR_PINK
};

CalibrationStep calibrationStep = CAL_IDLE;
as7341_gain_t   curGain         = AS7341_GAIN_8X;

// ================================================================
// Helpers
// ================================================================

void readChannels(uint16_t out[]) {
  out[CH_F1]  = as7341.getChannel(AS7341_CHANNEL_415nm_F1);
  out[CH_F2]  = as7341.getChannel(AS7341_CHANNEL_445nm_F2);
  out[CH_F3]  = as7341.getChannel(AS7341_CHANNEL_480nm_F3);
  out[CH_F4]  = as7341.getChannel(AS7341_CHANNEL_515nm_F4);
  out[CH_F5]  = as7341.getChannel(AS7341_CHANNEL_555nm_F5);
  out[CH_F6]  = as7341.getChannel(AS7341_CHANNEL_590nm_F6);
  out[CH_F7]  = as7341.getChannel(AS7341_CHANNEL_630nm_F7);
  out[CH_F8]  = as7341.getChannel(AS7341_CHANNEL_680nm_F8);
  out[CH_CLR] = as7341.getChannel(AS7341_CHANNEL_CLEAR);
  out[CH_NIR] = as7341.getChannel(AS7341_CHANNEL_NIR);
}

void printChannelsU16(const char* title, const uint16_t d[]) {
  Serial.println(title);
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Serial.print("  ");
    Serial.print(CH_NAMES[i]);
    Serial.print(": ");
    Serial.println(d[i]);
  }
}

bool captureAverageSimple(uint16_t averaged[], int targetSamples) {
  uint32_t sums[NUM_CHANNELS] = {0};
  int goodReads = 0;
  int retries   = 0;

  while (goodReads < targetSamples && retries < targetSamples * 4) {
    if (!as7341.readAllChannels()) {
      retries++;
      delay(60);
      continue;
    }
    uint16_t currentRead[NUM_CHANNELS];
    readChannels(currentRead);
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) sums[i] += currentRead[i];
    goodReads++;
    delay(60);
  }

  if (goodReads == 0) return false;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) averaged[i] = sums[i] / goodReads;
  return true;
}

// Auto-gain routine: run once during colorless calibration to find a
// gain level that produces peak counts in the "nice" range (not too
// dim, not saturated). Matches Jim's sketch.
void autoGainForTubeSetup() {
  for (int attempt = 0; attempt < 6; attempt++) {
    if (!as7341.readAllChannels()) { delay(100); continue; }

    uint16_t values[NUM_CHANNELS];
    readChannels(values);
    uint16_t peak = 0;
    for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
      if (values[i] > peak) peak = values[i];
    }

    if (peak >= 50000 && curGain > AS7341_GAIN_0_5X) {
      curGain = (as7341_gain_t)((int)curGain - 1);
      as7341.setGain(curGain);
      delay(120);
      continue;
    }
    if (peak < 2000 && curGain < AS7341_GAIN_256X) {
      curGain = (as7341_gain_t)((int)curGain + 1);
      as7341.setGain(curGain);
      delay(120);
      continue;
    }
    break;
  }
  Serial.print("Selected gain level: "); Serial.println((int)curGain);
}

bool referenceSignalValid(const uint16_t ref[]) {
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    if (ref[i] > darkRef[i] + 100) return true;
  }
  return false;
}

bool fullCalibrationComplete() {
  return darkCalibrated && colorlessCalibrated &&
         paleYellowCalibrated && yellowCalibrated &&
         darkYellowCalibrated && pinkCalibrated;
}

// ================================================================
// Calibration prompts
// ================================================================

void askForDarkCalibration() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("STEP 1 of 6: DARK REFERENCE");
  Serial.println("====================================");
  Serial.println("Block the optical path completely");
  Serial.println("(cover sensor / remove any tube).");
  Serial.println("Send 'y' when ready.");
  calibrationStep = CAL_WAITING_FOR_DARK;
}

void askForColorlessCalibration() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("STEP 2 of 6: COLORLESS REFERENCE");
  Serial.println("====================================");
  Serial.println("Insert a CLEAR / COLORLESS sample");
  Serial.println("(distilled water works well).");
  Serial.println("Send 'y' when ready.");
  calibrationStep = CAL_WAITING_FOR_COLORLESS;
}

void askForPaleYellowCalibration() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("STEP 3 of 6: PALE YELLOW REFERENCE");
  Serial.println("====================================");
  Serial.println("Insert a PALE YELLOW sample.");
  Serial.println("Send 'y' when ready.");
  calibrationStep = CAL_WAITING_FOR_PALE_YELLOW;
}

void askForYellowCalibration() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("STEP 4 of 6: YELLOW REFERENCE");
  Serial.println("====================================");
  Serial.println("Insert a YELLOW sample.");
  Serial.println("Send 'y' when ready.");
  calibrationStep = CAL_WAITING_FOR_YELLOW;
}

void askForDarkYellowCalibration() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("STEP 5 of 6: DARK YELLOW REFERENCE");
  Serial.println("====================================");
  Serial.println("Insert a DARK YELLOW / AMBER sample.");
  Serial.println("Send 'y' when ready.");
  calibrationStep = CAL_WAITING_FOR_DARK_YELLOW;
}

void askForPinkCalibration() {
  Serial.println();
  Serial.println("====================================");
  Serial.println("STEP 6 of 6: PINK / RED REFERENCE");
  Serial.println("====================================");
  Serial.println("Insert a PINK or RED sample");
  Serial.println("(diluted red food coloring works).");
  Serial.println("Send 'y' when ready.");
  calibrationStep = CAL_WAITING_FOR_PINK;
}

// ================================================================
// Calibration runners
// ================================================================

void captureNamedReference(const char* name, uint16_t refBuffer[], bool &flagOut) {
  Serial.println();
  Serial.print(">>> Capturing "); Serial.print(name); Serial.println(" reference...");
  delay(500);

  if (!captureAverageSimple(refBuffer, REF_SAMPLES)) {
    Serial.print("ERROR: "); Serial.print(name); Serial.println(" capture failed");
    flagOut = false;
    calibrationStep = CAL_IDLE;
    return;
  }

  if (!referenceSignalValid(refBuffer)) {
    Serial.print("ERROR: "); Serial.print(name); Serial.println(" signal too weak");
    Serial.println("  - Check LED current (+/-) and tube position");
    Serial.println("  - Send 'c' to restart calibration");
    flagOut = false;
    calibrationStep = CAL_IDLE;
    return;
  }

  flagOut = true;
  printChannelsU16(">>> Captured values:", refBuffer);
  Serial.print("OK "); Serial.print(name); Serial.println(" calibration complete.");
}

void runDarkCalibration() {
  Serial.println();
  Serial.println(">>> Capturing dark reference...");
  delay(500);
  if (!captureAverageSimple(darkRef, DARK_SAMPLES)) {
    Serial.println("ERROR: Dark capture failed");
    darkCalibrated = false;
    calibrationStep = CAL_IDLE;
    return;
  }
  darkCalibrated = true;
  printChannelsU16(">>> Captured dark reference:", darkRef);
  Serial.println("OK Dark calibration complete.");
}

void runColorlessCalibration() {
  Serial.println();
  Serial.println(">>> Auto-adjusting gain for this optical setup...");
  autoGainForTubeSetup();
  delay(500);
  captureNamedReference("Colorless", colorlessRef, colorlessCalibrated);
}

void runPaleYellowCalibration() { captureNamedReference("Pale Yellow", paleYellowRef, paleYellowCalibrated); }
void runYellowCalibration()     { captureNamedReference("Yellow",      yellowRef,     yellowCalibrated);     }
void runDarkYellowCalibration() { captureNamedReference("Dark Yellow", darkYellowRef, darkYellowCalibrated); }
void runPinkCalibration()       { captureNamedReference("Pink / Red",  pinkRef,       pinkCalibrated);       }

// ================================================================
// THE KEY FUNCTION: print paste-ready C code
// ================================================================

// Map a gain enum value to its #define name so the output is readable
const char* gainEnumName(as7341_gain_t g) {
  switch (g) {
    case AS7341_GAIN_0_5X: return "AS7341_GAIN_0_5X";
    case AS7341_GAIN_1X:   return "AS7341_GAIN_1X";
    case AS7341_GAIN_2X:   return "AS7341_GAIN_2X";
    case AS7341_GAIN_4X:   return "AS7341_GAIN_4X";
    case AS7341_GAIN_8X:   return "AS7341_GAIN_8X";
    case AS7341_GAIN_16X:  return "AS7341_GAIN_16X";
    case AS7341_GAIN_32X:  return "AS7341_GAIN_32X";
    case AS7341_GAIN_64X:  return "AS7341_GAIN_64X";
    case AS7341_GAIN_128X: return "AS7341_GAIN_128X";
    case AS7341_GAIN_256X: return "AS7341_GAIN_256X";
    case AS7341_GAIN_512X: return "AS7341_GAIN_512X";
    default:               return "AS7341_GAIN_8X";
  }
}

void printArrayLine(const char* varName, const uint16_t arr[]) {
  Serial.print("const uint16_t ");
  Serial.print(varName);
  Serial.print("[10] = { ");
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Serial.print(arr[i]);
    if (i < NUM_CHANNELS - 1) Serial.print(", ");
  }
  Serial.println(" };");
}

void printCalibrationCode() {
  if (!fullCalibrationComplete()) {
    Serial.println();
    Serial.println("ERROR: Calibration is not complete.");
    Serial.print("  dark=");        Serial.print(darkCalibrated);
    Serial.print("  colorless=");   Serial.print(colorlessCalibrated);
    Serial.print("  paleYellow=");  Serial.print(paleYellowCalibrated);
    Serial.print("  yellow=");      Serial.print(yellowCalibrated);
    Serial.print("  darkYellow=");  Serial.print(darkYellowCalibrated);
    Serial.print("  pink=");        Serial.println(pinkCalibrated);
    Serial.println("Send 'c' to run full calibration first.");
    return;
  }

  Serial.println();
  Serial.println("========================================================");
  Serial.println("// ======= BEGIN COLOR SENSOR CALIBRATION VALUES =======");
  Serial.println("// Generated by AS7341_calibration.ino");
  Serial.println("// Paste this block into urine_monitor_combined_iterationN.ino");
  Serial.println("// inside the HARDCODED COLOR CALIBRATION section.");
  Serial.println();

  Serial.print("const as7341_gain_t HARDCODED_COLOR_GAIN = ");
  Serial.print(gainEnumName(curGain));
  Serial.println(";");

  Serial.print("const uint16_t HARDCODED_LED_CURRENT_MA = ");
  Serial.print(ledMA);
  Serial.println(";");
  Serial.println();

  printArrayLine("HARDCODED_DARK_REF",        darkRef);
  printArrayLine("HARDCODED_COLORLESS_REF",   colorlessRef);
  printArrayLine("HARDCODED_PALE_YELLOW_REF", paleYellowRef);
  printArrayLine("HARDCODED_YELLOW_REF",      yellowRef);
  printArrayLine("HARDCODED_DARK_YELLOW_REF", darkYellowRef);
  printArrayLine("HARDCODED_PINK_REF",        pinkRef);

  Serial.println();
  Serial.println("// ======= END COLOR SENSOR CALIBRATION VALUES =======");
  Serial.println("========================================================");
  Serial.println();
  Serial.println("Copy everything between the BEGIN and END markers");
  Serial.println("(including the marker lines themselves) and paste it");
  Serial.println("into the combined iteration sketch.");
}

// ================================================================
// Status helpers
// ================================================================

void printStatus() {
  Serial.println();
  Serial.println("--- Calibration status ---");
  Serial.print("  Dark:        "); Serial.println(darkCalibrated       ? "YES" : "NO");
  Serial.print("  Colorless:   "); Serial.println(colorlessCalibrated  ? "YES" : "NO");
  Serial.print("  Pale Yellow: "); Serial.println(paleYellowCalibrated ? "YES" : "NO");
  Serial.print("  Yellow:      "); Serial.println(yellowCalibrated     ? "YES" : "NO");
  Serial.print("  Dark Yellow: "); Serial.println(darkYellowCalibrated ? "YES" : "NO");
  Serial.print("  Pink/Red:    "); Serial.println(pinkCalibrated       ? "YES" : "NO");
  Serial.print("  LED current mA: "); Serial.println(ledMA);
  Serial.print("  Gain level:     "); Serial.println((int)curGain);
  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println("AS7341 Calibration Utility");
  Serial.println("---------------------------");
  Serial.println("Commands:");
  Serial.println("  c = Start / restart full calibration");
  Serial.println("  y = Confirm current calibration step");
  Serial.println("  p = Print pasteable C code (when all 6 refs done)");
  Serial.println("  ? = Show status");
  Serial.println("  + = Increase LED current by 2 mA");
  Serial.println("  - = Decrease LED current by 2 mA");
  Serial.println("  h = This help");
  Serial.println();
}

// ================================================================
// Setup / Loop
// ================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(1);

  if (!as7341.begin()) {
    Serial.println("ERROR: Could not find AS7341. Check wiring.");
    while (1) delay(100);
  }

  Serial.println("=== AS7341 CALIBRATION UTILITY ===");

  as7341.setATIME(59);
  as7341.setASTEP(999);
  as7341.setGain(curGain);
  as7341.enableLED(true);
  as7341.setLEDCurrent(ledMA);

  printHelp();
  Serial.println("Send 'c' to begin the 6-step calibration.");
  Serial.println();
}

void loop() {
  if (!Serial.available()) return;

  char command = Serial.read();

  // Handle calibration confirmations first
  if ((command == 'y' || command == 'Y') && calibrationStep != CAL_IDLE) {
    switch (calibrationStep) {
      case CAL_WAITING_FOR_DARK:         runDarkCalibration();        askForColorlessCalibration();  break;
      case CAL_WAITING_FOR_COLORLESS:    runColorlessCalibration();   askForPaleYellowCalibration(); break;
      case CAL_WAITING_FOR_PALE_YELLOW:  runPaleYellowCalibration();  askForYellowCalibration();     break;
      case CAL_WAITING_FOR_YELLOW:       runYellowCalibration();      askForDarkYellowCalibration(); break;
      case CAL_WAITING_FOR_DARK_YELLOW:  runDarkYellowCalibration();  askForPinkCalibration();       break;
      case CAL_WAITING_FOR_PINK:
        runPinkCalibration();
        calibrationStep = CAL_IDLE;
        if (fullCalibrationComplete()) {
          Serial.println();
          Serial.println("====================================");
          Serial.println("ALL 6 REFERENCES CAPTURED");
          Serial.println("====================================");
          Serial.println("Send 'p' to print pasteable C code.");
        }
        break;
      default: break;
    }
    return;
  }

  if (command == 'c' || command == 'C') {
    // Clear state and restart
    darkCalibrated = colorlessCalibrated = false;
    paleYellowCalibrated = yellowCalibrated = false;
    darkYellowCalibrated = pinkCalibrated = false;
    askForDarkCalibration();
    return;
  }

  if (command == 'p' || command == 'P') { printCalibrationCode(); return; }
  if (command == '?')                   { printStatus();          return; }
  if (command == 'h' || command == 'H') { printHelp();            return; }

  if (command == '+') {
    if (ledMA < 150) { ledMA += 2; as7341.setLEDCurrent(ledMA); }
    Serial.print("LED current mA: "); Serial.println(ledMA);
    return;
  }
  if (command == '-') {
    if (ledMA > 2)   { ledMA -= 2; as7341.setLEDCurrent(ledMA); }
    Serial.print("LED current mA: "); Serial.println(ledMA);
    return;
  }
}
