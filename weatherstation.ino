/*
  ESP32 Weather Station with 2.13" BWR ePaper Display
  Optimized for battery use: sensors powered via one GPIO, deep sleep
  DHT11 sensor stabilization fix included
  No Serial output to save power
  Display only updates if sensor readings change by a meaningful amount

  Pinout:

  --- ePaper Module (WeActStudio 2.13" BWR) ---
  CS   -> ESP32 GPIO 5
  DC   -> ESP32 GPIO 17
  RST  -> ESP32 GPIO 16
  BUSY -> ESP32 GPIO 4
  VCC  -> 3.3V
  GND  -> GND

  --- BMP180 (I2C Pressure Sensor) ---
  VCC  -> ESP32 GPIO 15 (shared sensor power)
  GND  -> GND
  SDA  -> ESP32 GPIO 21
  SCL  -> ESP32 GPIO 22

  --- BH1750FVI (I2C Light Sensor) ---
  VCC  -> ESP32 GPIO 15 (shared sensor power)
  GND  -> GND
  SDA  -> ESP32 GPIO 21
  SCL  -> ESP32 GPIO 22
  ADDR -> GND  (sets I2C address 0x23)

  --- DHT11 (Temperature/Humidity Sensor) ---
  VCC -> ESP32 GPIO 15 (shared sensor power)
  GND -> GND
  DATA -> ESP32 GPIO 19
  (Use 10k pull-up if required)

  --- Shared sensor power GPIO ---
  D15 -> supplies 3.3V to DHT11, BMP180, BH1750 when HIGH
*/

// ================== Configuration ==================
#define INTERVAL_MINUTES 15  // measurement & update interval in minutes

#define ENABLE_GxEPD2_GFX 0

#include <WiFi.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeSansBold9pt7b.h>

#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <BH1750.h>
#include <DHT.h>

// ================== ePaper (BWR) ==================
#define CS_PIN 5
#define BUSY_PIN 4
#define RES_PIN 16
#define DC_PIN 17

// Use GxEPD2_BW for black and white ePaper module:
// https://github.com/WeActStudio/WeActStudio.EpaperModule/blob/master/Example/EpaperModuleTest_Arduino_ESP32/EpaperModuleTest_Arduino_ESP32.ino
GxEPD2_3C<GxEPD2_213_Z98c, GxEPD2_213_Z98c::HEIGHT> display(
  GxEPD2_213_Z98c(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN));

// ================== Sensors ==================
#define DHTPIN 19
#define DHTTYPE DHT11
#define SENSORS_VCC 15  // shared GPIO for all sensor power

DHT dht(DHTPIN, DHTTYPE);
Adafruit_BMP085 bmp;
BH1750 lightMeter;

// ================== Trend Data ==================
#define MAX_POINTS 20
RTC_DATA_ATTR float tempHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR float humHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR float pressureHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR float luxHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR int historyIndex = 0;

// --- Store last displayed readings ---
RTC_DATA_ATTR float lastTemp = NAN;
RTC_DATA_ATTR float lastHum = NAN;
RTC_DATA_ATTR float lastPressure = NAN;
RTC_DATA_ATTR float lastLux = NAN;

// --- Thresholds for meaningful changes ---
const float TEMP_THRESHOLD = 0.5f;      // °C
const float HUM_THRESHOLD = 2.0f;       // %
const float PRESSURE_THRESHOLD = 0.5f;  // hPa
const float LUX_THRESHOLD = 10.0f;      // lux

// ================== Setup ==================
void setup() {
  setCpuFrequencyMhz(80);
  btStop();             // disable Bluetooth
  WiFi.mode(WIFI_OFF);  // disable Wi-Fi

  // --- Sensor power pin ---
  pinMode(SENSORS_VCC, OUTPUT);
  digitalWrite(SENSORS_VCC, HIGH);  // turn on sensors first

  // --- Initialize I2C sensors first (overlaps DHT warmup) ---
  Wire.begin(21, 22);  // SDA, SCL
  bmp.begin();
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  // --- Short stabilization delay for DHT11 (warmup) ---
  delay(100);  // 100 ms is sufficient for sensors to power up

  // --- Initialize DHT11 sensor ---
  dht.begin();

  // --- Initialize ePaper display (takes ~10 seconds) ---
  display.init(115200, true, 50, false);
  display.setRotation(1);

  const int boxW = 190;
  const int boxH = 25;
  const int spacing = 5;
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  // --- Read sensors ---
  float temp = NAN;
  float hum = NAN;
  int retries = 0;
  const int maxRetries = 5;

  float pressure = bmp.readPressure() / 100.0;
  float lux = lightMeter.readLightLevel();

  while ((isnan(temp) || isnan(hum)) && retries < maxRetries) {
    temp = dht.readTemperature();
    hum = dht.readHumidity();
    if (isnan(temp) || isnan(hum)) delay(500);  // wait before retry
    retries++;
  }

  // --- Save in history arrays ---
  tempHistory[historyIndex] = temp;
  humHistory[historyIndex] = hum;
  pressureHistory[historyIndex] = pressure;
  luxHistory[historyIndex] = lux;
  historyIndex = (historyIndex + 1) % MAX_POINTS;

  // --- Determine if display update is needed ---
  bool updateNeeded = false;
  if (isnan(lastTemp) || fabs(temp - lastTemp) >= TEMP_THRESHOLD) updateNeeded = true;
  if (isnan(lastHum) || fabs(hum - lastHum) >= HUM_THRESHOLD) updateNeeded = true;
  if (isnan(lastPressure) || fabs(pressure - lastPressure) >= PRESSURE_THRESHOLD) updateNeeded = true;
  if (isnan(lastLux) || fabs(lux - lastLux) >= LUX_THRESHOLD) updateNeeded = true;

  if (updateNeeded) {
    updateDisplay();
    // --- Save last displayed values ---
    lastTemp = temp;
    lastHum = hum;
    lastPressure = pressure;
    lastLux = lux;
  }

  // --- Turn off sensors to save power ---
  digitalWrite(SENSORS_VCC, LOW);

  // --- Deep sleep for defined interval ---
  esp_sleep_enable_timer_wakeup(INTERVAL_MINUTES * 60 * 1000000ULL);
  esp_deep_sleep_start();
}

// ================== Loop ==================
void loop() {
  // Empty: all work done in setup() before deep sleep
}

// ================== Display ==================
void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    const int x = 2;
    int y = 3;
    const int boxW = 190;
    const int boxH = 25;
    const int spacing = 5;

    // --- TEMP box ---
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Temp: ");
    display.print(tempHistory[(historyIndex - 1 + MAX_POINTS) % MAX_POINTS], 1);
    display.print(" C");

    // --- HUMIDITY box ---
    y += boxH + spacing;
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Hum: ");
    display.print(humHistory[(historyIndex - 1 + MAX_POINTS) % MAX_POINTS], 0);
    display.print(" %");

    // --- PRESSURE box ---
    y += boxH + spacing;
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Pressure: ");
    display.print(pressureHistory[(historyIndex - 1 + MAX_POINTS) % MAX_POINTS], 1);
    display.print(" hPa");

    // --- LIGHT box ---
    y += boxH + spacing;
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Light: ");
    display.print(luxHistory[(historyIndex - 1 + MAX_POINTS) % MAX_POINTS], 0);
    display.print(" lux");

    // --- Draw trend lines with min/max markers ---
    const int trendX = boxW + 10;
    const int trendW = 55;
    const int trendH = 20;
    int trendY;

    // TEMP trend
    trendY = 3 + boxH / 2 - trendH / 2;
    drawTrendWithMinMax(tempHistory, trendX, trendY, trendW, trendH, MAX_POINTS);

    // HUMIDITY trend
    trendY = 3 + (boxH + spacing) + boxH / 2 - trendH / 2;
    drawTrendWithMinMax(humHistory, trendX, trendY, trendW, trendH, MAX_POINTS);

    // PRESSURE trend
    trendY = 3 + 2 * (boxH + spacing) + boxH / 2 - trendH / 2;
    drawTrendWithMinMax(pressureHistory, trendX, trendY, trendW, trendH, MAX_POINTS);

    // LIGHT trend
    trendY = 3 + 3 * (boxH + spacing) + boxH / 2 - trendH / 2;
    drawTrendWithMinMax(luxHistory, trendX, trendY, trendW, trendH, MAX_POINTS);

  } while (display.nextPage());
}

// ================== Draw Trend with Min/Max Markers ==================
void drawTrendWithMinMax(float *data, int x, int y, int w, int h, int count) {
  float minVal = data[0];
  float maxVal = data[0];

  for (int i = 1; i < count; i++) {
    float v = data[i];
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }

  if (minVal == maxVal) maxVal = minVal + 1.0f;

  float scale = (float)h / (maxVal - minVal);

  int lastX = x;
  int lastY = y + h - (int)((data[0] - minVal) * scale);

  for (int i = 1; i < count; i++) {
    int px = x + (i * w) / (count - 1);
    int py = y + h - (int)((data[i] - minVal) * scale);

    display.drawLine(lastX, lastY, px, py, GxEPD_BLACK);

    lastX = px;
    lastY = py;
  }

  display.drawFastHLine(x, y + h, w, GxEPD_BLACK);  // min marker
  display.drawFastHLine(x, y, w, GxEPD_BLACK);      // max marker
}
