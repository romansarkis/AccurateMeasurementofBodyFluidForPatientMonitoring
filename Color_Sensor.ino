#include <Adafruit_AS7341.h>
#include <math.h>

Adafruit_AS7341 as7341;

static const uint8_t NUM_SPECTRAL = 8;
static const uint8_t NUM_CHANNELS = 10;

static const int DARK_SAMPLES = 20;
static const int REF_SAMPLES = 20;
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

struct ColorResult {
  const char* name;
  const char* detail;
  float confidence;
};

struct ReferenceInfo {
  const char* name;
  uint16_t* rawRef;
};

uint16_t darkRef[NUM_CHANNELS];
uint16_t ambientRef[NUM_CHANNELS];
uint16_t colorlessRef[NUM_CHANNELS];
uint16_t paleYellowRef[NUM_CHANNELS];
uint16_t yellowRef[NUM_CHANNELS];
uint16_t darkYellowRef[NUM_CHANNELS];
uint16_t pinkRef[NUM_CHANNELS];

bool ambientCalibrated = false;
bool darkCalibrated = false;
bool colorlessCalibrated = false;
bool paleYellowCalibrated = false;
bool yellowCalibrated = false;
bool darkYellowCalibrated = false;
bool pinkCalibrated = false;
bool measurementEnabled = false;

enum CalibrationStep {
  CAL_IDLE,
  CAL_WAITING_FOR_AMBIENT,
  CAL_WAITING_FOR_DARK,
  CAL_WAITING_FOR_COLORLESS,
  CAL_WAITING_FOR_PALE_YELLOW,
  CAL_WAITING_FOR_YELLOW,
  CAL_WAITING_FOR_DARK_YELLOW,
  CAL_WAITING_FOR_PINK
};

CalibrationStep calibrationStep = CAL_IDLE;

as7341_gain_t curGain = AS7341_GAIN_8X;

// --------------------------------------------------
// Basic helpers
// --------------------------------------------------

float fabsFloat(float x) {
  return (x < 0.0f) ? -x : x;
}

float maxFloat(float a, float b) {
  return (a > b) ? a : b;
}

float minFloat(float a, float b) {
  return (a < b) ? a : b;
}

float computeAverage(const float values[], uint8_t count) {
  float sum = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    sum += values[i];
  }
  return sum / (float)count;
}

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

void printChannelsU16(const char* title, const uint16_t d[], uint8_t count) {
  Serial.println(title);
  for (uint8_t i = 0; i < count; i++) {
    Serial.print("  ");
    Serial.print(CH_NAMES[i]);
    Serial.print(": ");
    Serial.println(d[i]);
  }
}

void printChannelsFloat(const char* title, const float d[], uint8_t count) {
  Serial.println(title);
  for (uint8_t i = 0; i < count; i++) {
    Serial.print("  ");
    Serial.print(CH_NAMES[i]);
    Serial.print(": ");
    Serial.println(d[i], 6);
  }
}

void printTransmittance(const float t[]) {
  Serial.println("Relative transmittance");
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    Serial.print("  ");
    Serial.print(CH_NAMES[i]);
    Serial.print(": ");
    Serial.println(t[i], 6);
  }
}

void printSetupStatus() {
  Serial.println();
  Serial.print("Ambient calibrated: "); Serial.println(ambientCalibrated ? "YES" : "NO");
  Serial.print("Dark calibrated: "); Serial.println(darkCalibrated ? "YES" : "NO");
  Serial.print("Colorless calibrated: "); Serial.println(colorlessCalibrated ? "YES" : "NO");
  Serial.print("Pale Yellow calibrated: "); Serial.println(paleYellowCalibrated ? "YES" : "NO");
  Serial.print("Yellow calibrated: "); Serial.println(yellowCalibrated ? "YES" : "NO");
  Serial.print("Dark Yellow calibrated: "); Serial.println(darkYellowCalibrated ? "YES" : "NO");
  Serial.print("Pink calibrated: "); Serial.println(pinkCalibrated ? "YES" : "NO");
  Serial.print("Measurement enabled: "); Serial.println(measurementEnabled ? "YES" : "NO");
  Serial.print("LED current mA: "); Serial.println(ledMA);
  Serial.print("Gain level: "); Serial.println((int)curGain);
  Serial.println();
}

bool fullCalibrationComplete() {
  return darkCalibrated &&
         colorlessCalibrated &&
         paleYellowCalibrated &&
         yellowCalibrated &&
         darkYellowCalibrated &&
         pinkCalibrated;
}

// --------------------------------------------------
// Auto gain
// --------------------------------------------------

void autoGainForTubeSetup() {
  for (int attempt = 0; attempt < 6; attempt++) {
    if (!as7341.readAllChannels()) {
      delay(100);
      continue;
    }

    uint16_t values[NUM_CHANNELS];
    readChannels(values);

    uint16_t peak = 0;
    for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
      if (values[i] > peak) {
        peak = values[i];
      }
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

  Serial.print("Gain level: ");
  Serial.println((int)curGain);
}

// --------------------------------------------------
// Capture helpers
// --------------------------------------------------

bool captureAverageSimple(uint16_t averaged[], int targetSamples) {
  uint32_t sums[NUM_CHANNELS] = {0};
  int goodReads = 0;
  int retries = 0;

  while (goodReads < targetSamples && retries < targetSamples * 4) {
    if (!as7341.readAllChannels()) {
      retries++;
      delay(60);
      continue;
    }

    uint16_t currentRead[NUM_CHANNELS];
    readChannels(currentRead);

    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      sums[i] += currentRead[i];
    }

    goodReads++;
    delay(60);
  }

  if (goodReads == 0) {
    return false;
  }

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    averaged[i] = sums[i] / goodReads;
  }

  return true;
}

bool captureAverageWithOutlierRejection(uint16_t averaged[], int targetSamples) {
  uint16_t reads[SAMPLE_SAMPLES][NUM_CHANNELS];
  int goodReads = 0;
  int retries = 0;

  while (goodReads < targetSamples && retries < targetSamples * 5) {
    if (!as7341.readAllChannels()) {
      retries++;
      delay(60);
      continue;
    }

    readChannels(reads[goodReads]);
    goodReads++;
    delay(60);
  }

  if (goodReads < 3) {
    return false;
  }

  float firstPassMean[NUM_CHANNELS];
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    uint32_t sum = 0;
    for (int s = 0; s < goodReads; s++) {
      sum += reads[s][ch];
    }
    firstPassMean[ch] = (float)sum / (float)goodReads;
  }

  bool keep[SAMPLE_SAMPLES];
  int keptCount = 0;

  for (int s = 0; s < goodReads; s++) {
    float score = 0.0f;
    for (uint8_t ch = 0; ch < NUM_SPECTRAL; ch++) {
      float diff = (float)reads[s][ch] - firstPassMean[ch];
      score += fabsFloat(diff);
    }

    float avgDeviation = score / (float)NUM_SPECTRAL;
    if (avgDeviation < 2500.0f) {
      keep[s] = true;
      keptCount++;
    } else {
      keep[s] = false;
    }
  }

  if (keptCount < 3) {
    for (int s = 0; s < goodReads; s++) {
      keep[s] = true;
    }
    keptCount = goodReads;
  }

  uint32_t finalSums[NUM_CHANNELS] = {0};

  for (int s = 0; s < goodReads; s++) {
    if (!keep[s]) {
      continue;
    }
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      finalSums[ch] += reads[s][ch];
    }
  }

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    averaged[ch] = finalSums[ch] / keptCount;
  }

  return true;
}

// --------------------------------------------------
// Calibration prompts
// --------------------------------------------------

void askForAmbientCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 0: Ambient Light (Optional) ===");
  Serial.println("======================================");
  Serial.println("Do NOT insert a tube.");
  Serial.println("Let the sensor see your normal environment.");
  Serial.println("Send 'y' to capture ambient reference, or 'n' to skip.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_AMBIENT;
}

void askForDarkCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 1: Dark Reference ===");
  Serial.println("======================================");
  Serial.println("Block the optical path completely.");
  Serial.println("Send 'y' when ready.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_DARK;
}

void askForColorlessCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 2: Colorless Reference ===");
  Serial.println("======================================");
  Serial.println("Insert COLORLESS urine sample.");
  Serial.println("Keep the tube stable and aligned.");
  Serial.println("Send 'y' when ready.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_COLORLESS;
}

void askForPaleYellowCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 3: Pale Yellow Reference ===");
  Serial.println("======================================");
  Serial.println("Insert PALE YELLOW urine sample.");
  Serial.println("Keep the tube stable and aligned.");
  Serial.println("Send 'y' when ready.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_PALE_YELLOW;
}

void askForYellowCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 4: Yellow Reference ===");
  Serial.println("======================================");
  Serial.println("Insert YELLOW urine sample.");
  Serial.println("Keep the tube stable and aligned.");
  Serial.println("Send 'y' when ready.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_YELLOW;
}

void askForDarkYellowCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 5: Dark Yellow Reference ===");
  Serial.println("======================================");
  Serial.println("Insert DARK YELLOW urine sample.");
  Serial.println("Keep the tube stable and aligned.");
  Serial.println("Send 'y' when ready.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_DARK_YELLOW;
}

void askForPinkCalibration() {
  Serial.println();
  Serial.println("======================================");
  Serial.println("=== STEP 6: Pink / Red Reference ===");
  Serial.println("======================================");
  Serial.println("Insert PINK / RED urine sample.");
  Serial.println("Keep the tube stable and aligned.");
  Serial.println("Send 'y' when ready.");
  Serial.println();
  calibrationStep = CAL_WAITING_FOR_PINK;
}

// --------------------------------------------------
// Calibration runners
// --------------------------------------------------

bool referenceSignalValid(const uint16_t ref[]) {
  bool validSignal = false;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    if (ref[i] > darkRef[i] + 100) {
      validSignal = true;
      break;
    }
  }
  return validSignal;
}

void runAmbientCalibration() {
  Serial.println();
  Serial.println(">>> Capturing ambient reference...");
  delay(500);

  if (!captureAverageSimple(ambientRef, DARK_SAMPLES)) {
    Serial.println("ERROR: Ambient calibration failed");
    ambientCalibrated = false;
    calibrationStep = CAL_IDLE;
    return;
  }

  ambientCalibrated = true;
  printChannelsU16(">>> Stored ambient reference", ambientRef, NUM_CHANNELS);
  Serial.println("✓ Ambient calibration COMPLETE");
  Serial.println();

  calibrationStep = CAL_WAITING_FOR_DARK;
}

void skipAmbientCalibration() {
  Serial.println();
  Serial.println(">>> Skipping ambient calibration");
  ambientCalibrated = false;
  calibrationStep = CAL_WAITING_FOR_DARK;
}

void runDarkCalibration() {
  Serial.println();
  Serial.println(">>> Capturing dark reference...");
  delay(500);

  if (!captureAverageSimple(darkRef, DARK_SAMPLES)) {
    Serial.println("ERROR: Dark calibration failed");
    darkCalibrated = false;
    calibrationStep = CAL_IDLE;
    return;
  }

  darkCalibrated = true;
  printChannelsU16(">>> Stored dark reference", darkRef, NUM_CHANNELS);
  Serial.println("✓ Dark calibration COMPLETE");
  Serial.println();

  calibrationStep = CAL_WAITING_FOR_COLORLESS;
}

void captureNamedReference(const char* name, uint16_t refBuffer[], bool &flagOut, int samples = REF_SAMPLES) {
  Serial.println();
  Serial.print(">>> Capturing ");
  Serial.print(name);
  Serial.println(" reference...");
  delay(500);

  if (!captureAverageSimple(refBuffer, samples)) {
    Serial.print("ERROR: ");
    Serial.print(name);
    Serial.println(" calibration failed");
    flagOut = false;
    calibrationStep = CAL_IDLE;
    return;
  }

  if (!referenceSignalValid(refBuffer)) {
    Serial.print("ERROR: ");
    Serial.print(name);
    Serial.println(" signal too low");
    Serial.println("Possible causes:");
    Serial.println("  1. LED current too low");
    Serial.println("  2. Tube position incorrect");
    Serial.println("  3. Sample alignment inconsistent");
    Serial.println("Send 'c' to restart calibration.");
    flagOut = false;
    calibrationStep = CAL_IDLE;
    return;
  }

  flagOut = true;
  printChannelsU16(">>> Stored reference", refBuffer, NUM_CHANNELS);
  Serial.print("✓ ");
  Serial.print(name);
  Serial.println(" calibration COMPLETE");
  Serial.println();
}

void runColorlessCalibration() {
  Serial.println();
  Serial.println(">>> Auto-adjusting gain for your setup...");
  autoGainForTubeSetup();
  delay(500);

  captureNamedReference("Colorless", colorlessRef, colorlessCalibrated);
  if (!colorlessCalibrated) {
    return;
  }

  calibrationStep = CAL_WAITING_FOR_PALE_YELLOW;
}

void runPaleYellowCalibration() {
  captureNamedReference("Pale Yellow", paleYellowRef, paleYellowCalibrated);
  if (!paleYellowCalibrated) {
    return;
  }

  calibrationStep = CAL_WAITING_FOR_YELLOW;
}

void runYellowCalibration() {
  captureNamedReference("Yellow", yellowRef, yellowCalibrated);
  if (!yellowCalibrated) {
    return;
  }

  calibrationStep = CAL_WAITING_FOR_DARK_YELLOW;
}

void runDarkYellowCalibration() {
  captureNamedReference("Dark Yellow", darkYellowRef, darkYellowCalibrated);
  if (!darkYellowCalibrated) {
    return;
  }

  calibrationStep = CAL_WAITING_FOR_PINK;
}

void runPinkCalibration() {
  captureNamedReference("Pink / Red", pinkRef, pinkCalibrated);
  if (!pinkCalibrated) {
    return;
  }

  Serial.println();
  Serial.println("======================================");
  Serial.println("✓ ALL CALIBRATION COMPLETE");
  Serial.println("======================================");
  Serial.println("Now insert TEST SAMPLE.");
  Serial.println("Send 's' to start measurement.");
  Serial.println();

  calibrationStep = CAL_IDLE;
}

// --------------------------------------------------
// Signal preprocessing
// --------------------------------------------------

void correctedSignal(const uint16_t raw[], float corrected[]) {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    int32_t value = (int32_t)raw[i] - (int32_t)darkRef[i];
    if (value < 0) {
      value = 0;
    }
    corrected[i] = (float)value;
  }
}

void smoothSpectralValues(float values[]) {
  float original[NUM_SPECTRAL];

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    original[i] = values[i];
  }

  values[0] = (original[0] + original[1]) / 2.0f;
  values[NUM_SPECTRAL - 1] = (original[NUM_SPECTRAL - 2] + original[NUM_SPECTRAL - 1]) / 2.0f;

  for (uint8_t i = 1; i < NUM_SPECTRAL - 1; i++) {
    values[i] = (original[i - 1] + original[i] + original[i + 1]) / 3.0f;
  }
}

void applySNV(float values[]) {
  float meanValue = 0.0f;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    meanValue += values[i];
  }
  meanValue /= (float)NUM_SPECTRAL;

  float variance = 0.0f;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    float diff = values[i] - meanValue;
    variance += diff * diff;
  }
  variance /= (float)NUM_SPECTRAL;

  float stddev = sqrt(variance);
  if (stddev < 0.000001f) {
    return;
  }

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    values[i] = (values[i] - meanValue) / stddev;
  }
}

void calcRelativeTransmittance(const uint16_t raw[], float trans[]) {
  float correctedSample[NUM_CHANNELS];
  float correctedColorless[NUM_CHANNELS];

  correctedSignal(raw, correctedSample);

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    int32_t value = (int32_t)colorlessRef[i] - (int32_t)darkRef[i];
    if (value < 1) {
      value = 1;
    }
    correctedColorless[i] = (float)value;
  }

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    trans[i] = correctedSample[i] / correctedColorless[i];

    if (trans[i] < 0.0f) {
      trans[i] = 0.0f;
    }
    if (trans[i] > 2.0f) {
      trans[i] = 2.0f;
    }
  }
}

void buildReferenceTransmittance(const uint16_t refRaw[], float refTrans[]) {
  float correctedRef[NUM_CHANNELS];
  float correctedColorless[NUM_CHANNELS];

  correctedSignal(refRaw, correctedRef);

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    int32_t value = (int32_t)colorlessRef[i] - (int32_t)darkRef[i];
    if (value < 1) {
      value = 1;
    }
    correctedColorless[i] = (float)value;
  }

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    refTrans[i] = correctedRef[i] / correctedColorless[i];

    if (refTrans[i] < 0.0f) {
      refTrans[i] = 0.0f;
    }
    if (refTrans[i] > 2.0f) {
      refTrans[i] = 2.0f;
    }
  }

  smoothSpectralValues(refTrans);
}

float computeColorlessReferenceDrift(const uint16_t raw[]) {
  float correctedSample[NUM_CHANNELS];
  float correctedColorless[NUM_CHANNELS];

  correctedSignal(raw, correctedSample);

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    int32_t value = (int32_t)colorlessRef[i] - (int32_t)darkRef[i];
    if (value < 1) {
      value = 1;
    }
    correctedColorless[i] = (float)value;
  }

  float totalPercentDifference = 0.0f;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    float diff = fabsFloat(correctedSample[i] - correctedColorless[i]) / correctedColorless[i];
    totalPercentDifference += diff;
  }

  return (totalPercentDifference / (float)NUM_SPECTRAL) * 100.0f;
}

bool isValidLightSignal(const uint16_t raw[]) {
  int strongChannels = 0;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    if ((int32_t)raw[i] > (int32_t)darkRef[i] + 300) {
      strongChannels++;
    }
  }
  return strongChannels >= 4;
}

// --------------------------------------------------
// Ambient light analysis
// --------------------------------------------------

float computeAmbientLightImpact(const uint16_t raw[]) {
  if (!ambientCalibrated) {
    return 0.0f;
  }

  float totalPercent = 0.0f;
  int activeChannels = 0;

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    int32_t ambientSignal = (int32_t)ambientRef[i] - (int32_t)darkRef[i];
    int32_t currentSignal = (int32_t)raw[i] - (int32_t)darkRef[i];

    if (currentSignal > 100) {
      float percentImpact = (float)ambientSignal / (float)currentSignal * 100.0f;
      if (percentImpact < 0.0f) percentImpact = 0.0f;
      if (percentImpact > 100.0f) percentImpact = 100.0f;

      totalPercent += percentImpact;
      activeChannels++;
    }
  }

  if (activeChannels == 0) {
    return 0.0f;
  }

  return totalPercent / (float)activeChannels;
}

const char* getAmbientLightQuality(float impactPercent) {
  if (impactPercent < 5.0f) return "Excellent";
  if (impactPercent < 15.0f) return "Good";
  if (impactPercent < 30.0f) return "Fair";
  if (impactPercent < 50.0f) return "Poor";
  return "Very Poor";
}

// --------------------------------------------------
// Classification
// --------------------------------------------------

float spectralDistanceWeighted(const float a[], const float b[]) {
  static const float weights[NUM_SPECTRAL] = {
    1.0f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.4f, 1.3f
  };

  float sum = 0.0f;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    float diff = a[i] - b[i];
    sum += weights[i] * diff * diff;
  }
  return sqrt(sum);
}

float computeRednessScore(const float t[]) {
  float blue  = (t[CH_F1] + t[CH_F2] + t[CH_F3]) / 3.0f;
  float green = (t[CH_F4] + t[CH_F5]) / 2.0f;
  float red   = (t[CH_F6] + t[CH_F7] + t[CH_F8]) / 3.0f;

  if (blue < 0.001f) blue = 0.001f;
  if (green < 0.001f) green = 0.001f;

  return (red / green) * 0.6f + (red / blue) * 0.4f;
}

ColorResult classifyColorMultiAnchor(const float sampleT[]) {
  ColorResult res = {"Unknown", "Unable to classify", 0.0f};

  float avgSample = computeAverage(sampleT, NUM_SPECTRAL);
  float redness = computeRednessScore(sampleT);

  static char detailBuffer[160];

  if (avgSample < 0.03f) {
    res.name = "Unknown";
    res.detail = "Signal too weak";
    res.confidence = 0.0f;
    return res;
  }

  if (avgSample > 1.45f) {
    res.name = "Unknown";
    res.detail = "Brighter than reference, likely alignment mismatch";
    res.confidence = 0.0f;
    return res;
  }

  float colorlessT[NUM_SPECTRAL];
  float paleYellowT[NUM_SPECTRAL];
  float yellowT[NUM_SPECTRAL];
  float darkYellowT[NUM_SPECTRAL];
  float pinkT[NUM_SPECTRAL];

  buildReferenceTransmittance(colorlessRef, colorlessT);
  buildReferenceTransmittance(paleYellowRef, paleYellowT);
  buildReferenceTransmittance(yellowRef, yellowT);
  buildReferenceTransmittance(darkYellowRef, darkYellowT);
  buildReferenceTransmittance(pinkRef, pinkT);

  float dColorless  = spectralDistanceWeighted(sampleT, colorlessT);
  float dPaleYellow = spectralDistanceWeighted(sampleT, paleYellowT);
  float dYellow     = spectralDistanceWeighted(sampleT, yellowT);
  float dDarkYellow = spectralDistanceWeighted(sampleT, darkYellowT);
  float dPink       = spectralDistanceWeighted(sampleT, pinkT);

  float bestDist = dColorless;
  const char* bestName = "Colorless";

  if (dPaleYellow < bestDist) { bestDist = dPaleYellow; bestName = "Pale Yellow"; }
  if (dYellow < bestDist)     { bestDist = dYellow;     bestName = "Yellow"; }
  if (dDarkYellow < bestDist) { bestDist = dDarkYellow; bestName = "Dark Yellow"; }
  if (dPink < bestDist)       { bestDist = dPink;       bestName = "Pink"; }

  // Guard pink classification so it only wins if redness is actually elevated
  if (bestName == "Pink" && redness < 1.10f) {
    bestDist = dColorless;
    bestName = "Colorless";

    if (dPaleYellow < bestDist) { bestDist = dPaleYellow; bestName = "Pale Yellow"; }
    if (dYellow < bestDist)     { bestDist = dYellow;     bestName = "Yellow"; }
    if (dDarkYellow < bestDist) { bestDist = dDarkYellow; bestName = "Dark Yellow"; }
  }

  float confidence = 1.0f - (bestDist / 0.45f);
  if (confidence < 0.0f) confidence = 0.0f;
  if (confidence > 0.98f) confidence = 0.98f;

  res.name = bestName;
  snprintf(detailBuffer, sizeof(detailBuffer),
           "dC=%.3f dPY=%.3f dY=%.3f dDY=%.3f dP=%.3f red=%.2f avg=%.3f",
           dColorless, dPaleYellow, dYellow, dDarkYellow, dPink, redness, avgSample);
  res.detail = detailBuffer;
  res.confidence = confidence;

  return res;
}

// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  if (!as7341.begin()) {
    Serial.println("Could not find AS7341");
    while (1) {
      delay(10);
    }
  }
  Serial.println("=== BOARD RESET / SETUP RUNNING ===");

  as7341.setATIME(59);
  as7341.setASTEP(999);
  as7341.setGain(curGain);

  as7341.enableLED(true);
  as7341.setLEDCurrent(ledMA);

  Serial.println("AS7341 Urine Color Analyzer - Multi Anchor");
  Serial.println("------------------------------------------");
  Serial.println("MAIN COMMANDS:");
  Serial.println("  c = Full calibration");
  Serial.println("  d = Dark calibration only");
  Serial.println("  s = Start measurement");
  Serial.println("  + = Increase LED current");
  Serial.println("  - = Decrease LED current");
  Serial.println();
  Serial.println("CALIBRATION FLOW:");
  Serial.println("  STEP 0: Ambient (optional)");
  Serial.println("  STEP 1: Dark");
  Serial.println("  STEP 2: Colorless");
  Serial.println("  STEP 3: Pale Yellow");
  Serial.println("  STEP 4: Yellow");
  Serial.println("  STEP 5: Dark Yellow");
  Serial.println("  STEP 6: Pink / Red");
  Serial.println();
  Serial.println("QUICK START:");
  Serial.println("  1. Send 'c'");
  Serial.println("  2. Follow prompts");
  Serial.println("  3. Insert test sample");
  Serial.println("  4. Send 's'");
  Serial.println();

  printSetupStatus();
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  if (Serial.available()) {
    char command = Serial.read();

    if ((command == 'y' || command == 'Y') && calibrationStep != CAL_IDLE) {
      if (calibrationStep == CAL_WAITING_FOR_AMBIENT) {
        runAmbientCalibration();
        askForDarkCalibration();
        return;
      }

      if (calibrationStep == CAL_WAITING_FOR_DARK) {
        runDarkCalibration();
        askForColorlessCalibration();
        return;
      }

      if (calibrationStep == CAL_WAITING_FOR_COLORLESS) {
        runColorlessCalibration();
        askForPaleYellowCalibration();
        return;
      }

      if (calibrationStep == CAL_WAITING_FOR_PALE_YELLOW) {
        runPaleYellowCalibration();
        askForYellowCalibration();
        return;
      }

      if (calibrationStep == CAL_WAITING_FOR_YELLOW) {
        runYellowCalibration();
        askForDarkYellowCalibration();
        return;
      }

      if (calibrationStep == CAL_WAITING_FOR_DARK_YELLOW) {
        runDarkYellowCalibration();
        askForPinkCalibration();
        return;
      }

      if (calibrationStep == CAL_WAITING_FOR_PINK) {
        runPinkCalibration();
        printSetupStatus();
        return;
      }
    }

    if ((command == 'n' || command == 'N') && calibrationStep == CAL_WAITING_FOR_AMBIENT) {
      skipAmbientCalibration();
      askForDarkCalibration();
      return;
    }

    if (command == 'c' || command == 'C') {
      measurementEnabled = false;
      askForAmbientCalibration();
      return;
    }

    if (command == 'd' || command == 'D') {
      measurementEnabled = false;
      askForDarkCalibration();
      return;
    }

    if (command == 's' || command == 'S') {
      if (!fullCalibrationComplete()) {
        Serial.println("ERROR: Complete full calibration first with 'c'");
        return;
      }

      measurementEnabled = true;
      Serial.println("Sample measurement started");
      return;
    }

    if (command == '+') {
      if (ledMA < 150) {
        ledMA += 2;
        as7341.setLEDCurrent(ledMA);
      }
      Serial.print("LED current mA: ");
      Serial.println(ledMA);
      return;
    }

    if (command == '-') {
      if (ledMA > 2) {
        ledMA -= 2;
        as7341.setLEDCurrent(ledMA);
      }
      Serial.print("LED current mA: ");
      Serial.println(ledMA);
      return;
    }
  }

  if (!fullCalibrationComplete()) {
    if (calibrationStep == CAL_IDLE) {
      Serial.println("Run calibration first");
      Serial.println("Send 'c' for full calibration");
      delay(2000);
    }
    return;
  }

  if (!measurementEnabled) {
    Serial.println("Waiting for sample tube...");
    Serial.println("Insert test sample, then send 's'");
    delay(1500);
    return;
  }

  uint16_t averagedRaw[NUM_CHANNELS];

  if (!captureAverageWithOutlierRejection(averagedRaw, SAMPLE_SAMPLES)) {
    Serial.println("Read error");
    delay(500);
    return;
  }

  if (!isValidLightSignal(averagedRaw)) {
    Serial.println("WARNING: Weak light signal");
    Serial.println("Increase LED current or check tube position");
    delay(800);
    return;
  }

  bool saturated = false;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    if (averagedRaw[i] >= 60000) {
      saturated = true;
      break;
    }
  }

  if (saturated) {
    Serial.println("WARNING: Saturation detected");
    Serial.println("Reduce LED current or increase distance slightly");
  }

  float driftPercent = computeColorlessReferenceDrift(averagedRaw);
  if (driftPercent > 35.0f) {
    Serial.println("WARNING: Measurement drift too large");
    Serial.println("Possible causes:");
    Serial.println("  1. Tube position changed");
    Serial.println("  2. Ambient light changed");
    Serial.println("  3. Sample not aligned with calibration setup");
    Serial.println("Please reposition the tube and measure again.");
    delay(1000);
    return;
  }

  float transmittance[NUM_SPECTRAL];
  float snvSpectrum[NUM_SPECTRAL];

  calcRelativeTransmittance(averagedRaw, transmittance);
  smoothSpectralValues(transmittance);

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    snvSpectrum[i] = transmittance[i];
  }
  applySNV(snvSpectrum);

  ColorResult result = classifyColorMultiAnchor(transmittance);

  printChannelsU16("Averaged raw channels", averagedRaw, NUM_CHANNELS);
  Serial.println();

  printTransmittance(transmittance);
  Serial.println();

  printChannelsFloat("SNV normalized spectrum", snvSpectrum, NUM_SPECTRAL);
  Serial.println();

  Serial.print("Colorless reference drift percent: ");
  Serial.println(driftPercent, 6);

  if (driftPercent < 8.0f) {
    Serial.println("Reference match: sample close to baseline geometry");
  } else if (driftPercent < 20.0f) {
    Serial.println("Reference match: moderate difference from baseline");
  } else {
    Serial.println("Reference match: significant difference from baseline");
  }

  if (ambientCalibrated) {
    float ambientImpact = computeAmbientLightImpact(averagedRaw);
    Serial.println();
    Serial.print("Ambient light impact: ");
    Serial.print(ambientImpact, 1);
    Serial.println("%");
    Serial.print("Environment quality: ");
    Serial.println(getAmbientLightQuality(ambientImpact));
  }

  Serial.println();
  Serial.print("Color: ");
  Serial.println(result.name);
  Serial.print("Detail: ");
  Serial.println(result.detail);
  Serial.print("Confidence: ");
  Serial.println(result.confidence, 6);
  Serial.println("================================");

  delay(1000);
}