// =====================================================
// URINE MONITOR - COMBINED FINAL
// Integrates: weight/scale, display/touch, audio alerts,
//             color sensor (AS7341), opacity-aware layout,
//             alert log, output log, view toggle, time sync
// =====================================================

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_RA8875.h>
#include <HX711_ADC.h>
#include <TimeLib.h>
#include <string.h>
#include <Adafruit_AS7341.h>
#include <math.h>

// =====================================================
// DISPLAY / TOUCH / AUDIO
// =====================================================
const int RA8875_CS  = 10;
const int RA8875_RST = 9;
const int SPEAKER_PIN = 8;

Adafruit_RA8875 tft = Adafruit_RA8875(RA8875_CS, RA8875_RST);

// =====================================================
// LOAD CELL
// =====================================================
#define HX711_DT  4
#define HX711_SCK 5
HX711_ADC scale(HX711_DT, HX711_SCK);

// =====================================================
// COLOR SENSOR (AS7341)
// =====================================================
Adafruit_AS7341 as7341;

static const uint8_t NUM_SPECTRAL  = 8;
static const uint8_t NUM_CHANNELS  = 10;

static const int DARK_SAMPLES   = 20;
static const int BLANK_SAMPLES  = 20;
static const int SAMPLE_SAMPLES = 12;

uint16_t ledMA = 4;

enum ChIdx { CH_F1, CH_F2, CH_F3, CH_F4, CH_F5, CH_F6, CH_F7, CH_F8, CH_CLR, CH_NIR };

struct ColorResult {
  const char* name;
  const char* detail;
  float       confidence;
};

// =====================================================
// HARDCODED COLOR CALIBRATION
// =====================================================
// These values come from AS7341_calibration.ino — run that sketch
// once per physical setup, send 'p' after completing all 6 reference
// captures, and paste the printed block in place of the values below.
//
// The placeholder values below will let the sketch COMPILE but will
// NOT produce accurate color classification. ALWAYS replace with real
// captured values before using the device clinically or for testing.
//
// Workflow (same as load cell calibration):
//   1. Flash AS7341_calibration.ino to the Arduino
//   2. Run full calibration ('c' then 'y' through all 6 steps)
//   3. Send 'p' to dump the code block
//   4. Replace everything between the BEGIN and END markers below
//   5. Re-flash this combined sketch — color sensor is ready immediately

// ======= BEGIN COLOR SENSOR CALIBRATION VALUES =======
// !!! PLACEHOLDER VALUES — REPLACE AFTER RUNNING AS7341_calibration.ino !!!
const as7341_gain_t HARDCODED_COLOR_GAIN = AS7341_GAIN_32X;
const uint16_t HARDCODED_LED_CURRENT_MA = 4;

const uint16_t HARDCODED_DARK_REF[10] = { 25, 24, 33, 51, 77, 235, 617, 519, 777, 81 };
const uint16_t HARDCODED_COLORLESS_REF[10] = { 181, 889, 1096, 1459, 2374, 2926, 3007, 1612, 9555, 606 };
const uint16_t HARDCODED_PALE_YELLOW_REF[10] = { 172, 797, 1100, 1651, 2467, 3259, 3218, 1681, 9768, 583 };
const uint16_t HARDCODED_YELLOW_REF[10] = { 120, 441, 812, 1133, 1632, 2125, 2243, 1272, 7980, 492 };
const uint16_t HARDCODED_DARK_YELLOW_REF[10] = { 141, 462, 930, 1419, 2161, 2864, 2884, 1546, 9157, 546 };
const uint16_t HARDCODED_PINK_REF[10] = { 119, 417, 469, 533, 1368, 2754, 2908, 1548, 6527, 438 };
// ======= END COLOR SENSOR CALIBRATION VALUES =======

// Working reference arrays — populated from HARDCODED_* at setup().
// All six are used by the multi-anchor classifier.
uint16_t darkRef[NUM_CHANNELS];
uint16_t colorlessRef[NUM_CHANNELS];
uint16_t paleYellowRef[NUM_CHANNELS];
uint16_t yellowRef[NUM_CHANNELS];
uint16_t darkYellowRef[NUM_CHANNELS];
uint16_t pinkRef[NUM_CHANNELS];

// Calibration flags — all auto-set to true once hardcoded values load.
// Interactive calibration (removed in iteration 8) previously used these.
bool darkCalibrated       = false;
bool colorlessCalibrated  = false;
bool paleYellowCalibrated = false;
bool yellowCalibrated     = false;
bool darkYellowCalibrated = false;
bool pinkCalibrated       = false;
bool measurementEnabled   = false;
bool colorSensorStarted   = false;

as7341_gain_t curGain = AS7341_GAIN_8X;

// abnormal color alerting
bool abnormalColorActive       = false;
bool abnormalColorAcknowledged = false;

// color polling
unsigned long lastColorPollTime = 0;
const unsigned long COLOR_POLL_INTERVAL = 3000;

// =====================================================
// CONFIG
// =====================================================
float calibrationFactor = 210.036758;
const float GRAMS_TO_ML = 1.0;

// bag alert thresholds
const int NEAR_FULL_ML = 2500;
const int FULL_ML      = 2900;

// Minimum volume increase to count as a patient urine addition.
// Set well above scale noise (~5 mL) so small drift never creates a log entry.
const int ADD_EVENT_THRESHOLD_ML = 25;

// After logging an add event, suppress new entries for this long.
// A typical void finishes within 60 s; the cooldown lets the bag settle and
// then collapses any continuing rise into one consolidated log entry instead
// of a chain of +25 mL entries for a single void.
const unsigned long ADD_EVENT_COOLDOWN_MS = 90000UL;

// weight filtering
float lastReading = 0;
const float THRESHOLD           = 5.0;
const int   STABLE_COUNT_REQUIRED = 5;
int stableCount = 0;

float displayedWeight  = 0;
int   downwardTrendCount = 0;

const float DROP_TO_ZERO_THRESHOLD  = 20.0;  // raised from 5.0 to handle scale offset/noise
const int   DOWNWARD_COUNT_REQUIRED = 8;

// =====================================================
// LEAK DETECTION
// =====================================================
// We check the bag weight on a fixed interval and watch for a sustained
// downward trend. Anything that looks like noise (too small) or looks
// like someone emptying the bag in one go (too large and a one-off)
// is ignored. A sustained slow-to-medium drop across several intervals
// = LEAK.
//
// IMPORTANT: leak detection runs off `currentWeight` (the fresh stable
// reading), NOT `displayedWeight` (which is damped by
// DOWNWARD_COUNT_REQUIRED and only updates in discrete jumps). Using
// the damped value makes slow drains undetectable because the signal
// quantizes to 0 between jumps.
unsigned long lastLeakCheckTime  = 0;
int           lastLeakCheckVolume = 0;
bool          leakMonitoringInitialized = false;

// Poll faster than before so detection latency is ~6s instead of ~9s.
const unsigned long LEAK_CHECK_INTERVAL   = 2000;

// Widened drop range: anything from a trickle (1 mL / 2s) up to a
// fast drain (200 mL / 2s = 6 L/min) should count as a leak. Above
// 200 mL / 2s is almost certainly an intentional dump that would
// also trigger DROP — so we still reset the trend in that case.
const int           LEAK_DROP_THRESHOLD       = 200;
const int           LEAK_MIN_DROP_THRESHOLD   = 1;
const int           LEAK_TREND_REQUIRED       = 3;
int leakTrendCount = 0;

// Once a leak is dismissed, leakAcknowledged stays true until either
// (a) RESET is pressed or (b) the leak clearly stops. We consider it
// stopped once we see this many consecutive no-drop intervals after
// the alert was acknowledged — re-arms leak detection so a second
// leak event in the same session can still alert.
const int LEAK_REARM_STABLE_INTERVALS = 3;
int leakStableAfterAckCount = 0;

// =====================================================
// EXTENDED ZERO OUTPUT (silent leak / urimeter valve left open)
// =====================================================
// If the small urimeter drain valve is open when the bag is installed,
// any urine the patient produces will leak out before it registers on
// the scale — weight stays near 0 forever. There is no weight signal
// to detect this from, so we fall back to a time-based check:
// "if the bag has read empty for an unusually long time, something
// is probably wrong (either the patient is anuric or the valve is
// open)". Either case warrants a nurse check.
//
// Default: 30 minutes. For bench testing this scenario, drop this to
// 60000UL (1 minute) temporarily.
const unsigned long EXTENDED_ZERO_TIMEOUT_MS = 30UL * 60UL * 1000UL;
const float         EXTENDED_ZERO_WEIGHT_G   = 20.0f;

unsigned long extendedZeroStartMs  = 0;
bool          extendedZeroTracking = false;
bool          extendedZeroAlertFired = false;
bool          extendedZeroAcknowledged = false;

// hourly output logging
unsigned long lastHourlyOutputLogTime = 0;
int           lastHourlyOutputVolume  = 0;
bool          hourlyOutputInitialized = false;
const unsigned long HOURLY_OUTPUT_INTERVAL = 3600000UL;

// timezone offset applied to incoming unix time from Python
const long TIMEZONE_OFFSET_SECONDS = -4L * 3600L;

// =====================================================
// SCREEN LAYOUT
// =====================================================
#define LEFT_X  0
#define LEFT_W  300
#define RIGHT_X 300
#define RIGHT_W 180

#define HEADER_Y 0
#define HEADER_H 40

// Compact volume box (shrunk to make room for color section)
#define VOLUME_X 40
#define VOLUME_Y 50
#define VOLUME_W 220
#define VOLUME_H 60

// Color / opacity section
#define COLOR_X 40
#define COLOR_Y 118
#define COLOR_W 220
#define COLOR_H 48

// Alert box
#define ALERT_X 40
#define ALERT_Y 180
#define ALERT_W 220
#define ALERT_H 40

// Bottom-left buttons
#define DISMISS_BTN_X 40
#define DISMISS_BTN_Y 225
#define DISMISS_BTN_W 85
#define DISMISS_BTN_H 32

#define RESET_BTN_X 135
#define RESET_BTN_Y 225
#define RESET_BTN_W 85
#define RESET_BTN_H 32

// Right-panel log area
#define LOG_Y      45
#define LOG_LINE_H 16

// Bottom-right VIEW button
#define VIEW_BTN_X 392
#define VIEW_BTN_Y 252
#define VIEW_BTN_W 70
#define VIEW_BTN_H 18

// =====================================================
// LOG STRUCTS
// =====================================================
#define MAX_ALERT_LOGS  5
#define MAX_OUTPUT_LOGS 5

struct AlertEvent {
  bool   used;
  time_t eventTime;
  char   label[22];
  int    volumeMl;
};

struct OutputEvent {
  bool   used;
  time_t eventTime;
  int    outputMl;
  int    bagVolumeMl;
};

AlertEvent alertLog[MAX_ALERT_LOGS];
int alertLogIndex = 0;

OutputEvent outputLog[MAX_OUTPUT_LOGS];
int outputLogIndex = 0;

// =====================================================
// UI / APP STATE
// =====================================================
bool displayStarted   = false;
int  lastShownVolume  = -99999;

unsigned long lastTouchHandledTime  = 0;
const unsigned long TOUCH_DEBOUNCE_MS = 350;
bool touchWasDown = false;

time_t lastAddedTime       = 0;
time_t lastColorUpdateTime = 0;

int previousDisplayedVolumeMl = 0;
unsigned long lastAddEventTime = 0;   // millis() timestamp of the last logged add event

bool          alertActive              = false;
bool          alertNeedsAcknowledgement = false;
unsigned long alertStartTime           = 0;

// per-alert acknowledgement flags
bool dropAcknowledged         = false;
bool nearFullAcknowledged     = false;
bool fullAcknowledged         = false;
bool leakAcknowledged         = false;

// current urine color
char  currentUrineColor[20]  = "Unknown";
char  previousUrineColor[20] = "--";
float currentColorConfidence = 0.0f;

// =====================================================
// ENUMS
// =====================================================
enum AlertType {
  ALERT_NONE,
  ALERT_DROP,
  ALERT_LEAK,
  ALERT_ABNORMAL_COLOR,
  ALERT_NEAR_FULL,
  ALERT_FULL,
  ALERT_EXTENDED_ZERO
};
AlertType currentAlert = ALERT_NONE;

enum RightPanelView {
  VIEW_ALERTS,
  VIEW_OUTPUT
};
RightPanelView currentView = VIEW_ALERTS;

// =====================================================
// FORWARD DECLARATIONS
// =====================================================
void drawStaticUI();
void drawLogPanel();
void drawAlertLog();
void drawOutputLog();
void drawLastAddedTime();
void drawClock();
void drawColorSection();
void updateVolumeDisplay(int volumeMl);
void updatePersistentAlert();
void clearAlertBox();
void drawAlertText(const char* msg, uint16_t bgColor);
void drawDismissButton();
void clearDismissButton();
void drawResetButton();
void drawViewButton();
void dismissAlert();
void nurseReset();
void checkTouchButtons();
void showDropAlert();
void showLeakAlert();
void showExtendedZeroAlert();
void resetLeakTracking(int volumeMl);
void resetHourlyOutputTracking(int volumeMl);
void checkHourlyOutputLog();
void logAlertEvent(const char* label, int volumeMl);
void logOutputEvent(int outputMl, int bagVolumeMl);
void updateWeightLogic();
void startDisplay();
void startScale();
void startColorSensor();
void handleSerialInput();
void processColorCommand(char command);
void pollColorSensor();
void playAlertSound();

// =====================================================
// BASIC HELPERS
// =====================================================
int weightToMl(float grams) {
  return (int)(grams * GRAMS_TO_ML + 0.5f);
}

time_t currentEventTime() {
  return now();
}

bool hasValidClock() {
  return year() >= 2024;
}

void formatClockTime(char* buffer, size_t len) {
  snprintf(buffer, len, "%02d:%02d:%02d", hour(), minute(), second());
}

void formatStoredTime(time_t t, char* buffer, size_t len) {
  if (t == 0) {
    snprintf(buffer, len, "--:--:--");
    return;
  }
  tmElements_t tm;
  breakTime(t, tm);
  snprintf(buffer, len, "%02d:%02d:%02d", tm.Hour, tm.Minute, tm.Second);
}

// =====================================================
// COLOR SENSOR HELPERS
// =====================================================
float fabsFloat(float v) { return v < 0.0f ? -v : v; }

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

void autoGainForTubeSetup() {
  for (int attempt = 0; attempt < 6; attempt++) {
    if (!as7341.readAllChannels()) { delay(100); continue; }

    uint16_t values[NUM_CHANNELS];
    readChannels(values);

    uint16_t peak = 0;
    for (uint8_t i = 0; i < NUM_SPECTRAL; i++)
      if (values[i] > peak) peak = values[i];

    if (peak >= 50000 && curGain > AS7341_GAIN_0_5X) {
      curGain = (as7341_gain_t)((int)curGain - 1);
      as7341.setGain(curGain); delay(120); continue;
    }
    if (peak < 2000 && curGain < AS7341_GAIN_256X) {
      curGain = (as7341_gain_t)((int)curGain + 1);
      as7341.setGain(curGain); delay(120); continue;
    }
    break;
  }
}

bool captureAverageSimple(uint16_t averaged[], int targetSamples) {
  uint32_t sums[NUM_CHANNELS] = {0};
  int goodReads = 0, retries = 0;

  while (goodReads < targetSamples && retries < targetSamples * 4) {
    if (!as7341.readAllChannels()) { retries++; delay(60); continue; }
    uint16_t cur[NUM_CHANNELS];
    readChannels(cur);
    for (uint8_t c = 0; c < NUM_CHANNELS; c++) sums[c] += cur[c];
    goodReads++;
    delay(60);
  }
  if (goodReads == 0) return false;
  for (uint8_t c = 0; c < NUM_CHANNELS; c++) averaged[c] = sums[c] / goodReads;
  return true;
}

bool captureAverageWithOutlierRejection(uint16_t averaged[], int targetSamples) {
  uint16_t reads[SAMPLE_SAMPLES][NUM_CHANNELS];
  int goodReads = 0, retries = 0;

  while (goodReads < targetSamples && retries < targetSamples * 5) {
    if (!as7341.readAllChannels()) { retries++; delay(60); continue; }
    readChannels(reads[goodReads]);
    goodReads++;
    delay(60);
  }
  if (goodReads < 3) return false;

  float firstPassMean[NUM_CHANNELS];
  for (uint8_t c = 0; c < NUM_CHANNELS; c++) {
    uint32_t sum = 0;
    for (int s = 0; s < goodReads; s++) sum += reads[s][c];
    firstPassMean[c] = (float)sum / (float)goodReads;
  }

  bool keep[SAMPLE_SAMPLES];
  int keptCount = 0;
  for (int s = 0; s < goodReads; s++) {
    float score = 0.0f;
    for (uint8_t ch = 0; ch < NUM_SPECTRAL; ch++)
      score += fabsFloat((float)reads[s][ch] - firstPassMean[ch]);
    keep[s] = (score / (float)NUM_SPECTRAL) < 2500.0f;
    if (keep[s]) keptCount++;
  }
  if (keptCount < 3) {
    for (int s = 0; s < goodReads; s++) keep[s] = true;
    keptCount = goodReads;
  }

  uint32_t finalSums[NUM_CHANNELS] = {0};
  for (int s = 0; s < goodReads; s++) {
    if (!keep[s]) continue;
    for (uint8_t c = 0; c < NUM_CHANNELS; c++) finalSums[c] += reads[s][c];
  }
  for (uint8_t c = 0; c < NUM_CHANNELS; c++) averaged[c] = finalSums[c] / keptCount;
  return true;
}

void correctedSignal(const uint16_t raw[], float corrected[]) {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    int32_t v = (int32_t)raw[i] - (int32_t)darkRef[i];
    corrected[i] = (float)(v < 0 ? 0 : v);
  }
}

void calcRelativeTransmittance(const uint16_t raw[], float trans[]) {
  float correctedSample[NUM_CHANNELS];
  correctedSignal(raw, correctedSample);

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    int32_t denom = (int32_t)colorlessRef[i] - (int32_t)darkRef[i];
    if (denom < 1) denom = 1;
    trans[i] = correctedSample[i] / (float)denom;
    if (trans[i] < 0.0f) trans[i] = 0.0f;
    if (trans[i] > 2.0f) trans[i] = 2.0f;
  }
}

void smoothSpectralValues(float v[]) {
  float o[NUM_SPECTRAL];
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) o[i] = v[i];
  v[0] = (o[0] + o[1]) / 2.0f;
  v[NUM_SPECTRAL - 1] = (o[NUM_SPECTRAL - 2] + o[NUM_SPECTRAL - 1]) / 2.0f;
  for (uint8_t i = 1; i < NUM_SPECTRAL - 1; i++)
    v[i] = (o[i - 1] + o[i] + o[i + 1]) / 3.0f;
}

bool isValidLightSignal(const uint16_t raw[]) {
  int strong = 0;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++)
    if ((int32_t)raw[i] > (int32_t)darkRef[i] + 300) strong++;
  return strong >= 4;
}

// =====================================================
// MULTI-ANCHOR COLOR CLASSIFIER (Jim's approach)
// =====================================================
// Builds a reference transmittance curve for each calibrated color
// anchor (colorless, pale yellow, yellow, dark yellow, pink) and
// picks the closest one via weighted spectral distance.
// Labels here intentionally match the 5 calibrated reference colors
// so the classifier can only output labels it has evidence for.
// Pink has an extra guard so it can only win if the redness ratio
// is actually elevated — prevents false alarms from dim samples.

void buildReferenceTransmittance(const uint16_t refRaw[], float refTrans[]) {
  float correctedRef[NUM_CHANNELS];
  correctedSignal(refRaw, correctedRef);

  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) {
    int32_t denom = (int32_t)colorlessRef[i] - (int32_t)darkRef[i];
    if (denom < 1) denom = 1;
    refTrans[i] = correctedRef[i] / (float)denom;
    if (refTrans[i] < 0.0f) refTrans[i] = 0.0f;
    if (refTrans[i] > 2.0f) refTrans[i] = 2.0f;
  }
  smoothSpectralValues(refTrans);
}

float spectralDistanceWeighted(const float a[], const float b[]) {
  // Weights bias the distance toward red/yellow bands since that's
  // where clinically-relevant color changes live.
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
  if (blue < 0.001f)  blue = 0.001f;
  if (green < 0.001f) green = 0.001f;
  return (red / green) * 0.6f + (red / blue) * 0.4f;
}

ColorResult classifyColor(const float sampleT[]) {
  ColorResult res = { "Unknown", "Unable to classify", 0.0f };

  // Average transmittance — used for early sanity checks
  float avgSample = 0.0f;
  for (uint8_t i = 0; i < NUM_SPECTRAL; i++) avgSample += sampleT[i];
  avgSample /= (float)NUM_SPECTRAL;

  float redness = computeRednessScore(sampleT);
  static char detailBuf[100];

  if (avgSample < 0.03f) {
    res.detail = "Signal too weak — check LED / tube position";
    return res;
  }
  if (avgSample > 1.45f) {
    res.detail = "Brighter than reference — tube misalignment likely";
    return res;
  }

  // Build reference transmittance vectors for each anchor
  float colorlessT[NUM_SPECTRAL];
  float paleYellowT[NUM_SPECTRAL];
  float yellowT[NUM_SPECTRAL];
  float darkYellowT[NUM_SPECTRAL];
  float pinkT[NUM_SPECTRAL];

  buildReferenceTransmittance(colorlessRef,   colorlessT);
  buildReferenceTransmittance(paleYellowRef,  paleYellowT);
  buildReferenceTransmittance(yellowRef,      yellowT);
  buildReferenceTransmittance(darkYellowRef,  darkYellowT);
  buildReferenceTransmittance(pinkRef,        pinkT);

  float dColorless  = spectralDistanceWeighted(sampleT, colorlessT);
  float dPaleYellow = spectralDistanceWeighted(sampleT, paleYellowT);
  float dYellow     = spectralDistanceWeighted(sampleT, yellowT);
  float dDarkYellow = spectralDistanceWeighted(sampleT, darkYellowT);
  float dPink       = spectralDistanceWeighted(sampleT, pinkT);

  float bestDist       = dColorless;
  const char* bestName = "Colorless";

  if (dPaleYellow < bestDist) { bestDist = dPaleYellow; bestName = "Pale Yellow"; }
  if (dYellow < bestDist)     { bestDist = dYellow;     bestName = "Yellow"; }
  if (dDarkYellow < bestDist) { bestDist = dDarkYellow; bestName = "Dark Yellow"; }
  if (dPink < bestDist)       { bestDist = dPink;       bestName = "Pink"; }

  // Guard: only allow Pink to win if redness is actually elevated.
  // Without this, any noisy dim sample can accidentally land closer
  // to the pink anchor by euclidean distance.
  if (strcmp(bestName, "Pink") == 0 && redness < 1.10f) {
    bestDist = dColorless;
    bestName = "Colorless";
    if (dPaleYellow < bestDist) { bestDist = dPaleYellow; bestName = "Pale Yellow"; }
    if (dYellow < bestDist)     { bestDist = dYellow;     bestName = "Yellow"; }
    if (dDarkYellow < bestDist) { bestDist = dDarkYellow; bestName = "Dark Yellow"; }
  }

  float confidence = 1.0f - (bestDist / 0.45f);
  if (confidence < 0.0f)  confidence = 0.0f;
  if (confidence > 0.98f) confidence = 0.98f;

  snprintf(detailBuf, sizeof(detailBuf),
           "dC=%.2f dPY=%.2f dY=%.2f dDY=%.2f dP=%.2f red=%.2f",
           dColorless, dPaleYellow, dYellow, dDarkYellow, dPink, redness);

  res.name       = bestName;
  res.detail     = detailBuf;
  res.confidence = confidence;
  return res;
}

bool isAbnormalColorLabel(const char* label) {
  // Clinical abnormalities: very dark / concentrated (dehydration) and
  // pink / red (possible blood). Pale Yellow / Yellow are normal.
  return strcmp(label, "Dark Yellow") == 0 ||
         strcmp(label, "Pink")        == 0 ||
         strcmp(label, "Red")         == 0 ||
         strcmp(label, "Pink / Red")  == 0 ||
         strcmp(label, "Amber")       == 0 ||
         strcmp(label, "Dark Amber")  == 0 ||
         strcmp(label, "Brown")       == 0;
}

void updateUrineColorState(const char* detectedColor, float confidence) {
  if (strcmp(currentUrineColor, detectedColor) != 0) {
    strncpy(previousUrineColor, currentUrineColor, sizeof(previousUrineColor) - 1);
    previousUrineColor[sizeof(previousUrineColor) - 1] = '\0';
    strncpy(currentUrineColor, detectedColor, sizeof(currentUrineColor) - 1);
    currentUrineColor[sizeof(currentUrineColor) - 1] = '\0';
    currentColorConfidence = confidence;
    lastColorUpdateTime = currentEventTime();
    drawColorSection();
  }

  if (isAbnormalColorLabel(currentUrineColor)) {
    abnormalColorActive = true;
  } else {
    abnormalColorActive       = false;
    abnormalColorAcknowledged = false;
  }
}

// =====================================================
// AUDIO — repeating beep while alert is active
// =====================================================
void playAlertSound() {
  static unsigned long lastBeep = 0;
  if (!alertActive) { noTone(SPEAKER_PIN); return; }

  unsigned long nowMs = millis();
  if (nowMs - lastBeep > 500) {
    switch (currentAlert) {
      case ALERT_DROP:          tone(SPEAKER_PIN, 2000, 200); break;
      case ALERT_LEAK:          tone(SPEAKER_PIN, 1200, 200); break;
      case ALERT_ABNORMAL_COLOR:tone(SPEAKER_PIN, 1500, 200); break;
      case ALERT_NEAR_FULL:     tone(SPEAKER_PIN, 1800, 200); break;
      case ALERT_FULL:          tone(SPEAKER_PIN, 2500, 300); break;
      case ALERT_EXTENDED_ZERO: tone(SPEAKER_PIN, 1000, 300); break;
      default:                  noTone(SPEAKER_PIN);           break;
    }
    lastBeep = nowMs;
  }
}

// =====================================================
// DISPLAY STARTUP
// =====================================================
void startDisplay() {
  if (tft.begin(RA8875_480x272)) {
    displayStarted = true;
    tft.displayOn(true);
    tft.GPIOX(true);
    tft.PWM1config(true, RA8875_PWM_CLK_DIV1024);
    tft.PWM1out(255);
    tft.touchEnable(true);

    drawStaticUI();
    updateVolumeDisplay(weightToMl(displayedWeight));
    drawColorSection();
    drawLastAddedTime();
    drawClock();
    drawLogPanel();
    updatePersistentAlert();
  } else {
    Serial.println("RA8875 not found");
    while (1);
  }
}

void startScale() {
  scale.begin();
  scale.start(2000);
  if (scale.getTareTimeoutFlag()) {
    Serial.println("Tare failed.");
    while (1);
  }
  scale.setCalFactor(calibrationFactor);
  Serial.println("Remove weight for tare...");
  delay(2000);
  scale.tare();
  Serial.println("Scale ready.");
}

void loadHardcodedColorCalibration() {
  // Copy the paste-in HARDCODED_* values into the working reference
  // arrays, then flip all calibration flags true. After this the
  // multi-anchor classifier is ready to go immediately — no 'c'
  // command, no interactive flow.
  memcpy(darkRef,       HARDCODED_DARK_REF,        sizeof(darkRef));
  memcpy(colorlessRef,  HARDCODED_COLORLESS_REF,   sizeof(colorlessRef));
  memcpy(paleYellowRef, HARDCODED_PALE_YELLOW_REF, sizeof(paleYellowRef));
  memcpy(yellowRef,     HARDCODED_YELLOW_REF,      sizeof(yellowRef));
  memcpy(darkYellowRef, HARDCODED_DARK_YELLOW_REF, sizeof(darkYellowRef));
  memcpy(pinkRef,       HARDCODED_PINK_REF,        sizeof(pinkRef));

  darkCalibrated       = true;
  colorlessCalibrated  = true;
  paleYellowCalibrated = true;
  yellowCalibrated     = true;
  darkYellowCalibrated = true;
  pinkCalibrated       = true;

  curGain = HARDCODED_COLOR_GAIN;
  ledMA   = HARDCODED_LED_CURRENT_MA;

  measurementEnabled = true;
}

void startColorSensor() {
  if (!as7341.begin()) {
    Serial.println("AS7341 not found");
    colorSensorStarted = false;
    return;
  }
  as7341.setATIME(59);
  as7341.setASTEP(999);

  // Apply hardcoded calibration BEFORE setting gain, so curGain is valid
  loadHardcodedColorCalibration();

  as7341.setGain(curGain);
  as7341.enableLED(true);
  as7341.setLEDCurrent(ledMA);
  colorSensorStarted = true;

  Serial.println("AS7341 ready — hardcoded calibration applied");
  Serial.print("  Gain level: "); Serial.println((int)curGain);
  Serial.print("  LED current mA: "); Serial.println(ledMA);
  Serial.println("  Measurement enabled immediately (no 'c' needed)");
  Serial.println("  To re-calibrate, flash AS7341_calibration.ino and");
  Serial.println("  paste the new values into HARDCODED COLOR CALIBRATION.");
}

// =====================================================
// UI DRAWING
// =====================================================
void drawStaticUI() {
  tft.fillScreen(RA8875_BLACK);
  tft.textMode();
  tft.textEnlarge(1);
  tft.textTransparent(RA8875_WHITE);

  tft.textSetCursor(20, 10);
  tft.textWrite("Urine Monitor");

  tft.drawFastHLine(0, HEADER_H, 480, RA8875_WHITE);
  tft.drawFastVLine(RIGHT_X, 0, 272, RA8875_WHITE);

  tft.drawRect(VOLUME_X, VOLUME_Y, VOLUME_W, VOLUME_H, RA8875_WHITE);
  tft.drawRect(COLOR_X,  COLOR_Y,  COLOR_W,  COLOR_H,  RA8875_WHITE);
  tft.drawRect(ALERT_X,  ALERT_Y,  ALERT_W,  ALERT_H,  RA8875_WHITE);

  clearDismissButton();
  drawResetButton();
  drawViewButton();
}

void drawViewButton() {
  if (!displayStarted) return;
  tft.fillRect(VIEW_BTN_X, VIEW_BTN_Y, VIEW_BTN_W, VIEW_BTN_H, RA8875_BLUE);
  tft.drawRect(VIEW_BTN_X, VIEW_BTN_Y, VIEW_BTN_W, VIEW_BTN_H, RA8875_WHITE);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(VIEW_BTN_X + 18, VIEW_BTN_Y + 1);
  tft.textWrite("VIEW");
}

void drawLogPanel() {
  if (!displayStarted) return;

  tft.fillRect(RIGHT_X + 1, 1, RIGHT_W - 2, HEADER_H - 2, RA8875_BLACK);
  tft.drawFastHLine(RIGHT_X, HEADER_H, RIGHT_W, RA8875_WHITE);

  tft.textMode();
  tft.textEnlarge(1);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(RIGHT_X + 10, 10);
  tft.textWrite(currentView == VIEW_ALERTS ? "Alert Log" : "Output Log");

  if (currentView == VIEW_ALERTS) drawAlertLog();
  else                            drawOutputLog();

  drawViewButton();
}

void updateVolumeDisplay(int volumeMl) {
  if (!displayStarted) return;
  if (volumeMl == lastShownVolume) return;
  lastShownVolume = volumeMl;

  tft.fillRect(VOLUME_X + 8, VOLUME_Y + 8, VOLUME_W - 16, VOLUME_H - 16, RA8875_BLACK);

  tft.textMode();
  tft.textEnlarge(1);
  if      (volumeMl >= FULL_ML)      tft.textTransparent(RA8875_RED);
  else if (volumeMl >= NEAR_FULL_ML) tft.textTransparent(RA8875_YELLOW);
  else                               tft.textTransparent(RA8875_CYAN);

  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%d mL", volumeMl);
  tft.textSetCursor(85, VOLUME_Y + 20);
  tft.textWrite(buffer);
}

void drawColorSection() {
  if (!displayStarted) return;

  tft.fillRect(COLOR_X + 1, COLOR_Y + 1, COLOR_W - 2, COLOR_H - 2, RA8875_BLACK);
  tft.textMode();
  tft.textEnlarge(0);

  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(COLOR_X + 8, COLOR_Y + 6);
  tft.textWrite("Color:");

  uint16_t colorText = RA8875_WHITE;
  if      (strcmp(currentUrineColor, "Colorless")   == 0) colorText = RA8875_CYAN;
  else if (strcmp(currentUrineColor, "Pale Yellow")  == 0) colorText = RA8875_WHITE;
  else if (strcmp(currentUrineColor, "Light Yellow") == 0) colorText = RA8875_YELLOW;
  else if (strcmp(currentUrineColor, "Yellow")       == 0) colorText = RA8875_YELLOW;
  else if (strcmp(currentUrineColor, "Dark Yellow")  == 0) colorText = RA8875_YELLOW;
  else if (strcmp(currentUrineColor, "Amber")        == 0) colorText = RA8875_RED;
  else if (strcmp(currentUrineColor, "Dark Amber")   == 0) colorText = RA8875_RED;
  else if (strcmp(currentUrineColor, "Brown")        == 0) colorText = RA8875_RED;
  else if (strcmp(currentUrineColor, "Pink")         == 0 ||
           strcmp(currentUrineColor, "Pink / Red")   == 0) colorText = RA8875_MAGENTA;
  else if (strcmp(currentUrineColor, "Red")          == 0) colorText = RA8875_RED;

  tft.textTransparent(colorText);
  tft.textSetCursor(COLOR_X + 55, COLOR_Y + 6);
  tft.textWrite(currentUrineColor);

  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(COLOR_X + 8, COLOR_Y + 24);

  if (lastColorUpdateTime == 0) {
    tft.textWrite("Last color: --");
  } else {
    char ts[20];
    formatStoredTime(lastColorUpdateTime, ts, sizeof(ts));
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "Last color: %s", ts);
    tft.textWrite(buffer);
  }
}

void clearAlertBox() {
  tft.fillRect(ALERT_X + 1, ALERT_Y + 1, ALERT_W - 2, ALERT_H - 2, RA8875_BLACK);
  tft.drawRect(ALERT_X, ALERT_Y, ALERT_W, ALERT_H, RA8875_WHITE);
}

void drawAlertText(const char* msg, uint16_t bgColor) {
  tft.fillRect(ALERT_X, ALERT_Y, ALERT_W, ALERT_H, bgColor);
  tft.drawRect(ALERT_X, ALERT_Y, ALERT_W, ALERT_H, RA8875_WHITE);
  tft.textMode();
  tft.textEnlarge(1);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(ALERT_X + 10, ALERT_Y + 4);
  tft.textWrite(msg);
}

void drawDismissButton() {
  if (!displayStarted) return;
  tft.fillRect(DISMISS_BTN_X, DISMISS_BTN_Y, DISMISS_BTN_W, DISMISS_BTN_H, RA8875_BLUE);
  tft.drawRect(DISMISS_BTN_X, DISMISS_BTN_Y, DISMISS_BTN_W, DISMISS_BTN_H, RA8875_WHITE);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(DISMISS_BTN_X + 12, DISMISS_BTN_Y + 10);
  tft.textWrite("DISMISS");
}

void clearDismissButton() {
  if (!displayStarted) return;
  tft.fillRect(DISMISS_BTN_X, DISMISS_BTN_Y, DISMISS_BTN_W, DISMISS_BTN_H, RA8875_BLACK);
}

void drawResetButton() {
  if (!displayStarted) return;
  tft.fillRect(RESET_BTN_X, RESET_BTN_Y, RESET_BTN_W, RESET_BTN_H, RA8875_RED);
  tft.drawRect(RESET_BTN_X, RESET_BTN_Y, RESET_BTN_W, RESET_BTN_H, RA8875_WHITE);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(RESET_BTN_X + 20, RESET_BTN_Y + 10);
  tft.textWrite("RESET");
}

void drawAlertLog() {
  if (!displayStarted) return;
  tft.fillRect(RIGHT_X + 5, LOG_Y, RIGHT_W - 10, 170, RA8875_BLACK);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);

  int screenLine = 0;
  for (int i = 0; i < MAX_ALERT_LOGS; i++) {
    int idx = (alertLogIndex - 1 - i + MAX_ALERT_LOGS) % MAX_ALERT_LOGS;
    if (!alertLog[idx].used) continue;

    char ts[20];
    formatStoredTime(alertLog[idx].eventTime, ts, sizeof(ts));
    tft.textSetCursor(RIGHT_X + 10, LOG_Y + screenLine * LOG_LINE_H);
    tft.textWrite(ts);
    screenLine++;
    if (screenLine >= 10) break;

    char buffer[48];
    snprintf(buffer, sizeof(buffer), "%s %dmL", alertLog[idx].label, alertLog[idx].volumeMl);
    tft.textSetCursor(RIGHT_X + 10, LOG_Y + screenLine * LOG_LINE_H);
    tft.textWrite(buffer);
    screenLine++;
    if (screenLine >= 10) break;
  }
}

void drawOutputLog() {
  if (!displayStarted) return;
  tft.fillRect(RIGHT_X + 5, LOG_Y, RIGHT_W - 10, 170, RA8875_BLACK);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);

  int screenLine = 0;
  for (int i = 0; i < MAX_OUTPUT_LOGS; i++) {
    int idx = (outputLogIndex - 1 - i + MAX_OUTPUT_LOGS) % MAX_OUTPUT_LOGS;
    if (!outputLog[idx].used) continue;

    // Line 1: "+NNN mL  HH:MM:SS"
    char ts[20];
    formatStoredTime(outputLog[idx].eventTime, ts, sizeof(ts));
    char buffer[48];
    snprintf(buffer, sizeof(buffer), "+%d mL  %s", outputLog[idx].outputMl, ts);
    tft.textSetCursor(RIGHT_X + 10, LOG_Y + screenLine * LOG_LINE_H);
    tft.textWrite(buffer);
    screenLine++;
    if (screenLine >= 10) break;

    // Line 2: "Total: NNN mL"
    snprintf(buffer, sizeof(buffer), "Total:%d mL", outputLog[idx].bagVolumeMl);
    tft.textSetCursor(RIGHT_X + 10, LOG_Y + screenLine * LOG_LINE_H);
    tft.textWrite(buffer);
    screenLine++;
    if (screenLine >= 10) break;
  }
}

void drawLastAddedTime() {
  if (!displayStarted) return;
  tft.fillRect(RIGHT_X + 5, 222, RIGHT_W - 10, 48, RA8875_BLACK);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(RIGHT_X + 10, 228);

  if (lastAddedTime == 0) {
    tft.textWrite("Last add: --");
    drawViewButton();
    return;
  }
  char ts[20];
  formatStoredTime(lastAddedTime, ts, sizeof(ts));
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "Last add: %s", ts);
  tft.textWrite(buffer);
  drawViewButton();
}

void drawClock() {
  if (!displayStarted) return;

  char timeStr[20];
  if (hasValidClock()) formatClockTime(timeStr, sizeof(timeStr));
  else                 snprintf(timeStr, sizeof(timeStr), "--:--:--");

  char buffer[24];
  snprintf(buffer, sizeof(buffer), "Now: %s", timeStr);

  tft.fillRect(RIGHT_X + 5, 246, 82, 18, RA8875_BLACK);
  tft.textMode();
  tft.textEnlarge(0);
  tft.textTransparent(RA8875_WHITE);
  tft.textSetCursor(RIGHT_X + 10, 248);
  tft.textWrite(buffer);
  drawViewButton();
}

// =====================================================
// LOGGING
// =====================================================
void logAlertEvent(const char* label, int volumeMl) {
  alertLog[alertLogIndex].used = true;
  alertLog[alertLogIndex].eventTime = currentEventTime();
  strncpy(alertLog[alertLogIndex].label, label, sizeof(alertLog[alertLogIndex].label) - 1);
  alertLog[alertLogIndex].label[sizeof(alertLog[alertLogIndex].label) - 1] = '\0';
  alertLog[alertLogIndex].volumeMl = volumeMl;
  alertLogIndex = (alertLogIndex + 1) % MAX_ALERT_LOGS;

  if (currentView == VIEW_ALERTS) { drawAlertLog(); drawViewButton(); }
}

void logOutputEvent(int outputMl, int bagVolumeMl) {
  outputLog[outputLogIndex].used       = true;
  outputLog[outputLogIndex].eventTime  = currentEventTime();
  outputLog[outputLogIndex].outputMl   = outputMl;
  outputLog[outputLogIndex].bagVolumeMl = bagVolumeMl;
  outputLogIndex = (outputLogIndex + 1) % MAX_OUTPUT_LOGS;

  if (currentView == VIEW_OUTPUT) { drawOutputLog(); drawViewButton(); }
}

// =====================================================
// ALERT / TRACKING HELPERS
// =====================================================
void resetLeakTracking(int volumeMl) {
  lastLeakCheckVolume = volumeMl;
  lastLeakCheckTime   = millis();
  leakTrendCount      = 0;
  leakMonitoringInitialized = true;
}

void resetHourlyOutputTracking(int volumeMl) {
  lastHourlyOutputVolume  = volumeMl;
  lastHourlyOutputLogTime = millis();
  hourlyOutputInitialized = true;
}

void showDropAlert() {
  alertActive              = true;
  alertNeedsAcknowledgement = true;
  alertStartTime           = millis();
  currentAlert             = ALERT_DROP;
  dropAcknowledged         = false;

  tone(SPEAKER_PIN, 2000, 300);
  drawAlertText("DROP DETECTED", RA8875_RED);
  drawDismissButton();
  logAlertEvent("DROP DETECTED", weightToMl(displayedWeight));
}

void showLeakAlert() {
  alertActive              = true;
  alertNeedsAcknowledgement = true;
  alertStartTime           = millis();
  currentAlert             = ALERT_LEAK;
  leakAcknowledged         = false;

  tone(SPEAKER_PIN, 1200, 300);
  drawAlertText("LEAK DETECTED", RA8875_YELLOW);
  drawDismissButton();
  logAlertEvent("LEAK DETECTED", weightToMl(displayedWeight));
  leakTrendCount = 0;
  leakStableAfterAckCount = 0;

  Serial.println("*** LEAK ALERT FIRED ***");
}

void showExtendedZeroAlert() {
  alertActive              = true;
  alertNeedsAcknowledgement = true;
  alertStartTime           = millis();
  currentAlert             = ALERT_EXTENDED_ZERO;
  extendedZeroAcknowledged = false;
  extendedZeroAlertFired   = true;

  tone(SPEAKER_PIN, 1000, 300);
  drawAlertText("NO OUTPUT - CHECK", RA8875_YELLOW);
  drawDismissButton();
  logAlertEvent("NO OUTPUT", 0);

  Serial.println("*** EXTENDED ZERO OUTPUT ALERT FIRED ***");
  Serial.println("    Bag has read near-empty continuously for longer");
  Serial.println("    than EXTENDED_ZERO_TIMEOUT_MS. Check for:");
  Serial.println("    - Urimeter drain valve left open");
  Serial.println("    - Main drain valve left open");
  Serial.println("    - Disconnected tubing");
  Serial.println("    - True anuria (clinical emergency)");
}

void dismissAlert() {
  if      (currentAlert == ALERT_DROP)          dropAcknowledged         = true;
  else if (currentAlert == ALERT_LEAK)          leakAcknowledged         = true;
  else if (currentAlert == ALERT_NEAR_FULL)     nearFullAcknowledged     = true;
  else if (currentAlert == ALERT_FULL)          fullAcknowledged         = true;
  else if (currentAlert == ALERT_ABNORMAL_COLOR) abnormalColorAcknowledged = true;
  else if (currentAlert == ALERT_EXTENDED_ZERO) extendedZeroAcknowledged = true;

  alertActive              = false;
  alertNeedsAcknowledgement = false;
  currentAlert             = ALERT_NONE;

  noTone(SPEAKER_PIN);
  clearAlertBox();
  clearDismissButton();
}

void nurseReset() {
  alertActive              = false;
  alertNeedsAcknowledgement = false;
  currentAlert             = ALERT_NONE;

  dropAcknowledged         = false;
  nearFullAcknowledged     = false;
  fullAcknowledged         = false;
  leakAcknowledged         = false;
  abnormalColorAcknowledged = false;
  abnormalColorActive      = false;

  displayedWeight          = 0;
  previousDisplayedVolumeMl = 0;
  lastShownVolume          = -99999;
  downwardTrendCount       = 0;
  stableCount              = 0;
  lastReading              = 0;

  scale.tare();   // re-zero the load cell on nurse reset

  lastAddedTime  = 0;
  lastAddEventTime = 0;

  leakTrendCount            = 0;
  leakMonitoringInitialized = false;
  lastLeakCheckTime         = 0;
  lastLeakCheckVolume       = 0;
  leakStableAfterAckCount   = 0;

  extendedZeroStartMs       = 0;
  extendedZeroTracking      = false;
  extendedZeroAlertFired    = false;
  extendedZeroAcknowledged  = false;

  hourlyOutputInitialized   = false;
  lastHourlyOutputLogTime   = 0;
  lastHourlyOutputVolume    = 0;

  strcpy(currentUrineColor,  "Unknown");
  strcpy(previousUrineColor, "--");
  lastColorUpdateTime   = 0;
  currentColorConfidence = 0.0f;

  for (int i = 0; i < MAX_ALERT_LOGS; i++) {
    alertLog[i].used      = false;
    alertLog[i].eventTime = 0;
    alertLog[i].label[0]  = '\0';
    alertLog[i].volumeMl  = 0;
  }
  alertLogIndex = 0;

  for (int i = 0; i < MAX_OUTPUT_LOGS; i++) {
    outputLog[i].used        = false;
    outputLog[i].eventTime   = 0;
    outputLog[i].outputMl    = 0;
    outputLog[i].bagVolumeMl = 0;
  }
  outputLogIndex = 0;

  noTone(SPEAKER_PIN);
  clearAlertBox();
  clearDismissButton();
  updateVolumeDisplay(0);
  drawColorSection();
  drawLastAddedTime();
  drawClock();
  drawResetButton();
  drawLogPanel();
}

void updatePersistentAlert() {
  int volumeMl = weightToMl(displayedWeight);

  // Priority 1: DROP
  if (currentAlert == ALERT_DROP && alertActive && !dropAcknowledged) {
    alertNeedsAcknowledgement = true;
    drawAlertText("DROP DETECTED", RA8875_RED);
    drawDismissButton();
    return;
  }

  // Priority 2: LEAK
  if (currentAlert == ALERT_LEAK && alertActive && !leakAcknowledged) {
    alertNeedsAcknowledgement = true;
    drawAlertText("LEAK DETECTED", RA8875_YELLOW);
    drawDismissButton();
    return;
  }

  // Priority 2b: EXTENDED ZERO OUTPUT (possible silent leak / anuria)
  if (currentAlert == ALERT_EXTENDED_ZERO && alertActive && !extendedZeroAcknowledged) {
    alertNeedsAcknowledgement = true;
    drawAlertText("NO OUTPUT - CHECK", RA8875_YELLOW);
    drawDismissButton();
    return;
  }

  // Priority 3: ABNORMAL COLOR
  if (abnormalColorActive && !abnormalColorAcknowledged) {
    currentAlert              = ALERT_ABNORMAL_COLOR;
    alertActive               = true;
    alertNeedsAcknowledgement = true;
    drawAlertText("ABNORMAL COLOR", RA8875_MAGENTA);
    drawDismissButton();
    logAlertEvent("ABNORMAL COLOR", volumeMl);
    return;
  }

  // Priority 4: BAG FULL
  if (volumeMl >= FULL_ML) {
    nearFullAcknowledged = false;
    if (!fullAcknowledged) {
      currentAlert              = ALERT_FULL;
      alertActive               = true;
      alertNeedsAcknowledgement = true;
      tone(SPEAKER_PIN, 2500, 300);
      drawAlertText("BAG FULL", RA8875_RED);
      drawDismissButton();
      logAlertEvent("BAG FULL", volumeMl);
      return;
    }
  } else {
    fullAcknowledged = false;
  }

  // Priority 5: NEAR FULL
  if (volumeMl >= NEAR_FULL_ML && volumeMl < FULL_ML) {
    if (!nearFullAcknowledged) {
      currentAlert              = ALERT_NEAR_FULL;
      alertActive               = true;
      alertNeedsAcknowledgement = true;
      tone(SPEAKER_PIN, 1800, 300);
      drawAlertText("NEAR FULL", RA8875_YELLOW);
      drawDismissButton();
      logAlertEvent("NEAR FULL", volumeMl);
      return;
    }
  } else {
    nearFullAcknowledged = false;
  }

  if (currentAlert != ALERT_DROP) dropAcknowledged = false;

  alertActive               = false;
  alertNeedsAcknowledgement = false;
  currentAlert              = ALERT_NONE;
  noTone(SPEAKER_PIN);
  clearAlertBox();
  clearDismissButton();
}

void checkHourlyOutputLog() {
  int volumeMl       = weightToMl(displayedWeight);
  unsigned long nowMs = millis();

  if (!hourlyOutputInitialized) { resetHourlyOutputTracking(volumeMl); return; }

  if (nowMs - lastHourlyOutputLogTime >= HOURLY_OUTPUT_INTERVAL) {
    int outputMl = volumeMl - lastHourlyOutputVolume;
    if (outputMl < 0) outputMl = 0;
    logOutputEvent(outputMl, volumeMl);
    lastHourlyOutputVolume  = volumeMl;
    lastHourlyOutputLogTime = nowMs;
  }
}

// =====================================================
// TOUCH HANDLING
// =====================================================
void checkTouchButtons() {
  // Always call touchRead() immediately after touched() so the RA8875
  // hardware clears its touch register on every pass. Skipping touchRead()
  // (e.g. by returning early when "locked") leaves the register set, which
  // makes touched() return true forever — the root cause of buttons only
  // working once.
  if (!tft.touched()) return;

  uint16_t rawX, rawY;
  tft.touchRead(&rawX, &rawY);  // must be called every time to clear hardware flag

  unsigned long nowMs = millis();
  if (nowMs - lastTouchHandledTime < TOUCH_DEBOUNCE_MS) return;

  uint16_t x = constrain(map(rawX, 76,  961, 0, 479), 0, 479);
  uint16_t y = constrain(map(rawY, 127, 900, 0, 271), 0, 271);

  Serial.print("Touch X: "); Serial.print(x);
  Serial.print(" Y: ");       Serial.println(y);

  const int pad = 10;

  // VIEW button — works regardless of alert state
  if (x >= VIEW_BTN_X - pad && x <= VIEW_BTN_X + VIEW_BTN_W + pad &&
      y >= VIEW_BTN_Y - pad && y <= VIEW_BTN_Y + VIEW_BTN_H + pad) {
    Serial.println("VIEW BUTTON PRESSED");
    currentView = (currentView == VIEW_ALERTS) ? VIEW_OUTPUT : VIEW_ALERTS;
    drawLogPanel();
    drawClock();
    lastTouchHandledTime = nowMs;
    return;
  }

  // DISMISS button
  if (alertNeedsAcknowledgement &&
      x >= DISMISS_BTN_X - pad && x <= DISMISS_BTN_X + DISMISS_BTN_W + pad &&
      y >= DISMISS_BTN_Y - pad && y <= DISMISS_BTN_Y + DISMISS_BTN_H + pad) {
    Serial.println("DISMISS PRESSED");
    dismissAlert();
    lastTouchHandledTime = nowMs;
    return;
  }

  // RESET button
  if (x >= RESET_BTN_X - pad && x <= RESET_BTN_X + RESET_BTN_W + pad &&
      y >= RESET_BTN_Y - pad && y <= RESET_BTN_Y + RESET_BTN_H + pad) {
    Serial.println("RESET PRESSED");
    nurseReset();
    lastTouchHandledTime = nowMs;
    return;
  }
}

// =====================================================
// WEIGHT LOGIC
// =====================================================
void updateWeightLogic() {
  scale.update();
  float currentWeight = scale.getData();

  if (abs(currentWeight) > 10000) {
    Serial.println("Passed load cell capacity");
    return;
  }

  float diff = abs(currentWeight - lastReading);
  if (diff > THRESHOLD) stableCount = 0;
  else                  stableCount++;

  if (stableCount >= STABLE_COUNT_REQUIRED) {
    if (abs(currentWeight) < 1.0f) currentWeight = 0;

    // Sudden drop to near-zero: bag emptied / void event
    if (displayedWeight > 50 && currentWeight < DROP_TO_ZERO_THRESHOLD) {
      int droppedMl = weightToMl(displayedWeight);
      Serial.print("Detected sudden drop -> void event: ");
      Serial.print(droppedMl);
      Serial.println(" mL");

      showDropAlert();
      displayedWeight   = 0;
      downwardTrendCount = 0;
      resetLeakTracking(0);
      resetHourlyOutputTracking(0);

    } else if (currentWeight < displayedWeight) {
      // Gradual decrease: require trend before accepting
      downwardTrendCount++;
      if (downwardTrendCount >= DOWNWARD_COUNT_REQUIRED) {
        displayedWeight    = currentWeight;
        downwardTrendCount = 0;
      }
    } else {
      displayedWeight    = currentWeight;
      downwardTrendCount = 0;
    }

    int volumeMl       = weightToMl(displayedWeight);
    int actualVolumeMl = weightToMl(currentWeight);  // undamped — for leak detection
    unsigned long nowMs = millis();

    // --- EXTENDED ZERO OUTPUT TRACKING ---
    // If the bag reads near-empty continuously, start a timer. If it
    // stays near-empty past EXTENDED_ZERO_TIMEOUT_MS, fire an alert —
    // this catches the case where the urimeter drain valve was left
    // open before the bag was installed, so produced urine never
    // accumulates.
    if (currentWeight < EXTENDED_ZERO_WEIGHT_G) {
      if (!extendedZeroTracking) {
        extendedZeroTracking = true;
        extendedZeroStartMs  = nowMs;
        Serial.println("Extended-zero timer started (bag reads near-empty)");
      } else if (!extendedZeroAlertFired && !extendedZeroAcknowledged &&
                 (nowMs - extendedZeroStartMs >= EXTENDED_ZERO_TIMEOUT_MS)) {
        // Extended-zero alert supersedes FULL/NEAR_FULL (which shouldn't be active anyway at ~0)
        // but not DROP/LEAK which are higher priority.
        bool canFire = !alertActive ||
                       currentAlert == ALERT_NEAR_FULL ||
                       currentAlert == ALERT_FULL ||
                       currentAlert == ALERT_ABNORMAL_COLOR;
        if (canFire) showExtendedZeroAlert();
      }
    } else {
      if (extendedZeroTracking) {
        Serial.println("Extended-zero timer cleared (weight registered)");
      }
      extendedZeroTracking   = false;
      extendedZeroStartMs    = 0;
      extendedZeroAlertFired = false;
      // Note: extendedZeroAcknowledged intentionally not cleared here —
      // only RESET rearms that, to prevent repeat firing during a single session.
    }

    // --- LEAK DETECTION ---
    // Uses actualVolumeMl (undamped) so slow drains are visible immediately
    // instead of being quantized by the downward-trend damping on displayedWeight.
    if (!leakMonitoringInitialized) {
      resetLeakTracking(actualVolumeMl);
    } else if (nowMs - lastLeakCheckTime >= LEAK_CHECK_INTERVAL) {
      int volumeDrop = lastLeakCheckVolume - actualVolumeMl;

      Serial.print("Leak check: prev=");
      Serial.print(lastLeakCheckVolume);
      Serial.print(" mL  now=");
      Serial.print(actualVolumeMl);
      Serial.print(" mL  drop=");
      Serial.print(volumeDrop);
      Serial.print(" mL  trend=");

      if (volumeDrop >= LEAK_MIN_DROP_THRESHOLD && volumeDrop <= LEAK_DROP_THRESHOLD) {
        leakTrendCount++;
        leakStableAfterAckCount = 0;
        Serial.print(leakTrendCount);
        Serial.print("/");
        Serial.println(LEAK_TREND_REQUIRED);
      } else {
        if (leakTrendCount > 0) {
          Serial.print("reset (was ");
          Serial.print(leakTrendCount);
          Serial.println(")");
        } else {
          Serial.println("0");
        }
        leakTrendCount = 0;

        // If the leak was previously acknowledged, count clean intervals.
        // Enough clean intervals in a row = leak has stopped, re-arm so a
        // future leak can fire a new alert.
        if (leakAcknowledged) {
          leakStableAfterAckCount++;
          if (leakStableAfterAckCount >= LEAK_REARM_STABLE_INTERVALS) {
            leakAcknowledged        = false;
            leakStableAfterAckCount = 0;
            Serial.println("Leak detection re-armed (no drops for several intervals)");
          }
        }
      }

      // Allow LEAK to supersede lower-priority alerts (NEAR_FULL, FULL,
      // ABNORMAL_COLOR). Do NOT supersede DROP (higher priority) or an
      // existing LEAK. This matters for the test scenario: a 2900 mL
      // full bag fires FULL immediately on boot, which would otherwise
      // block leak detection.
      bool canFire = !alertActive ||
                     currentAlert == ALERT_NEAR_FULL ||
                     currentAlert == ALERT_FULL ||
                     currentAlert == ALERT_ABNORMAL_COLOR ||
                     currentAlert == ALERT_EXTENDED_ZERO;
      if (canFire && !leakAcknowledged && leakTrendCount >= LEAK_TREND_REQUIRED)
        showLeakAlert();

      lastLeakCheckVolume = actualVolumeMl;
      lastLeakCheckTime   = nowMs;
    }

    // Volume increase event — only fire when:
    //   1. Volume has risen by at least ADD_EVENT_THRESHOLD_ML since the last
    //      baseline (filters out scale noise / slow drift).
    //   2. The post-event cooldown has elapsed (consolidates a single slow void
    //      into one log entry instead of a chain of smaller ones).
    if (volumeMl >= previousDisplayedVolumeMl + ADD_EVENT_THRESHOLD_ML &&
        (lastAddEventTime == 0 || nowMs - lastAddEventTime >= ADD_EVENT_COOLDOWN_MS)) {
      int addedMl = volumeMl - previousDisplayedVolumeMl;
      lastAddedTime = currentEventTime();
      drawLastAddedTime();
      drawClock();
      leakTrendCount = 0;
      resetLeakTracking(actualVolumeMl);
      // New urine arriving means whatever leak was acknowledged is stale;
      // re-arm so a subsequent leak gets a fresh alert.
      leakAcknowledged        = false;
      leakStableAfterAckCount = 0;
      Serial.print("Add event: +");
      Serial.print(addedMl);
      Serial.print(" mL  Total: ");
      Serial.print(volumeMl);
      Serial.println(" mL");
      logOutputEvent(addedMl, volumeMl);  // record in output log
      // Only advance the baseline when an event actually fires so that
      // addedMl reflects the full accumulation since the last logged entry,
      // not just the last small jump that happened to cross the threshold.
      previousDisplayedVolumeMl = volumeMl;
      lastAddEventTime = nowMs;
    } else if (volumeMl < previousDisplayedVolumeMl) {
      // Volume decreased — reset baseline so the next addition is measured
      // from the new (lower) level and doesn't log an inflated amount.
      previousDisplayedVolumeMl = volumeMl;
    }

    // Commented out for color sensor testing — re-enable when weight output is needed on serial
    // Serial.print("Displayed Volume: ");
    // Serial.print(volumeMl);
    // Serial.println(" mL");

    updateVolumeDisplay(volumeMl);
    updatePersistentAlert();
  }

  lastReading = currentWeight;
}

// =====================================================
// COLOR CALIBRATION / SERIAL COMMANDS
// =====================================================

// Channel labels paired with ChIdx order (F1..F8 + Clear + NIR).
// Kept local to the calibration printout so we don't clutter the global scope.
static const char* const COLOR_CH_LABELS[NUM_CHANNELS] = {
  "F1 415nm", "F2 445nm", "F3 480nm", "F4 515nm",
  "F5 555nm", "F6 590nm", "F7 630nm", "F8 680nm",
  "Clear   ", "Near IR "
};

// Dumps one calibration reference array to the serial monitor with
// per-channel labels, so the user can verify the sensor captured
// sensible values during calibration (or copy them for analysis).
void printCalibrationArray(const char* label, const uint16_t data[]) {
  Serial.print("--- ");
  Serial.print(label);
  Serial.println(" reference ---");
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Serial.print("  ");
    Serial.print(COLOR_CH_LABELS[i]);
    Serial.print(" : ");
    Serial.println(data[i]);
  }
}

// Prints every active calibration reference currently loaded in memory.
// In iteration 8 these are always populated from the HARDCODED_* arrays
// at the top of this sketch — this function is now a diagnostic tool
// for verifying what values the classifier is actually using.
void printAllCalibrationValues() {
  Serial.println();
  Serial.println("====== ACTIVE COLOR CALIBRATION (in memory) ======");
  Serial.print("Gain level (0=0.5X .. 10=512X): ");
  Serial.println((int)curGain);
  Serial.print("LED current mA: ");
  Serial.println(ledMA);
  Serial.println();

  printCalibrationArray("DARK",        darkRef);
  printCalibrationArray("COLORLESS",   colorlessRef);
  printCalibrationArray("PALE_YELLOW", paleYellowRef);
  printCalibrationArray("YELLOW",      yellowRef);
  printCalibrationArray("DARK_YELLOW", darkYellowRef);
  printCalibrationArray("PINK",        pinkRef);

  Serial.println("==================================================");
  Serial.println();
}

// Serial command handler for the color sensor.
// Iteration 8 removes the interactive calibration flow entirely —
// calibration now lives in the HARDCODED_* arrays at the top of this
// file, captured by AS7341_calibration.ino. These commands are just
// runtime convenience:
//   'p' — print the currently-loaded calibration values (diagnostic)
//   's' — toggle measurement on / off
//   '+' / '-' — nudge LED current (rarely needed since the hardcoded
//              value is applied at boot and is generally correct)
//   'c' — prints a reminder pointing the user at AS7341_calibration.ino
void processColorCommand(char command) {
  if (!colorSensorStarted) return;

  if (command == 'p' || command == 'P') {
    printAllCalibrationValues();
    return;
  }

  if (command == 's' || command == 'S') {
    measurementEnabled = !measurementEnabled;
    Serial.print("Color measurement: ");
    Serial.println(measurementEnabled ? "ON" : "OFF");
    return;
  }

  if (command == '+') {
    if (ledMA < 150) { ledMA += 2; as7341.setLEDCurrent(ledMA); }
    Serial.print("LED current mA: "); Serial.println(ledMA);
    return;
  }
  if (command == '-') {
    if (ledMA > 2) { ledMA -= 2; as7341.setLEDCurrent(ledMA); }
    Serial.print("LED current mA: "); Serial.println(ledMA);
    return;
  }

  if (command == 'c' || command == 'C') {
    Serial.println();
    Serial.println("NOTE: Interactive calibration was removed in iteration 8.");
    Serial.println("To re-calibrate the color sensor:");
    Serial.println("  1. Flash AS7341_calibration.ino");
    Serial.println("  2. Send 'c' then 'y' through all 6 reference steps");
    Serial.println("  3. Send 'p' to print the pasteable C code block");
    Serial.println("  4. Paste that block in place of HARDCODED COLOR");
    Serial.println("     CALIBRATION at the top of this sketch");
    Serial.println("  5. Re-flash this sketch");
    Serial.println();
    return;
  }
}

void handleSerialInput() {
  while (Serial.available() > 0) {
    int nextByte = Serial.peek();
    if (nextByte < 0) return;

    char c = (char)nextByte;

    // Numeric line = unix time sync from Python
    if ((c >= '0' && c <= '9') || c == '-') {
      String incoming = Serial.readStringUntil('\n');
      long unixTime   = incoming.toInt();
      if (unixTime > 1000000000L) {
        setTime(unixTime + TIMEZONE_OFFSET_SECONDS);
        Serial.print("Time synced: ");
        Serial.println(unixTime);
        drawLastAddedTime();
        drawClock();
        drawLogPanel();
        drawColorSection();
      }
    } else if (c == '\n' || c == '\r') {
      Serial.read();
    } else {
      char command = (char)Serial.read();
      processColorCommand(command);
    }
  }
}

void pollColorSensor() {
  if (!colorSensorStarted || !measurementEnabled) return;

  unsigned long nowMs = millis();
  if (nowMs - lastColorPollTime < COLOR_POLL_INTERVAL) return;
  lastColorPollTime = nowMs;

  uint16_t averagedRaw[NUM_CHANNELS];
  if (!captureAverageWithOutlierRejection(averagedRaw, SAMPLE_SAMPLES)) {
    Serial.println("Color read error");
    return;
  }
  if (!isValidLightSignal(averagedRaw)) {
    Serial.println("Weak color signal");
    return;
  }

  float transmittance[NUM_SPECTRAL];
  calcRelativeTransmittance(averagedRaw, transmittance);
  smoothSpectralValues(transmittance);

  ColorResult result = classifyColor(transmittance);

  Serial.print("Color: ");       Serial.print(result.name);
  Serial.print(" | Confidence: "); Serial.println(result.confidence, 3);

  updateUrineColorState(result.name, result.confidence);
  drawColorSection();
  updatePersistentAlert();
}

// =====================================================
// SETUP / LOOP
// =====================================================
void setup() {
  Serial.begin(9600);
  SPI.begin();
  Wire.begin();
  pinMode(SPEAKER_PIN, OUTPUT);

  startDisplay();
  startScale();
  startColorSensor();

  resetLeakTracking(weightToMl(displayedWeight));
  resetHourlyOutputTracking(weightToMl(displayedWeight));
}

void loop() {
  handleSerialInput();
  updateWeightLogic();
  pollColorSensor();
  drawClock();
  checkHourlyOutputLog();
  checkTouchButtons();
  playAlertSound();   // repeating beep while any alert is active
  delay(50);
}
