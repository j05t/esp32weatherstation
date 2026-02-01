/*
  ESP32 Weather Station with 2.13" BWR ePaper Display
  Optimized for battery use: sensors powered via one GPIO, deep sleep
  DHT11 sensor stabilization fix included
  No Serial output to save power

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

#define ENABLE_GxEPD2_GFX 0

#include <WiFi.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeSansBold9pt7b.h>

#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <BH1750.h>
#include <DHT.h>

// ================== ePaper ==================
#define CS_PIN 5
#define BUSY_PIN 4
#define RES_PIN 16
#define DC_PIN 17

// 2.13'' EPD Module
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

// ================== Measurement Interval ==================
#define INTERVAL_MINUTES 10  // measurement & update interval in minutes

// ================== Trend Data ==================
#define MAX_POINTS 20
RTC_DATA_ATTR float tempHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR float humHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR float pressureHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR float luxHistory[MAX_POINTS] = { 0 };
RTC_DATA_ATTR int historyIndex = 0;
RTC_DATA_ATTR bool historyValid = false;

// ================== Setup ==================
void setup() {
  setCpuFrequencyMhz(80);
  btStop();  // disable Bluetooth
  WiFi.mode(WIFI_OFF);

  // --- Sensor power pin ---
  pinMode(SENSORS_VCC, OUTPUT);
  digitalWrite(SENSORS_VCC, HIGH);  // turn on sensors first

  // --- Short stabilization delay for DHT11 ---
  delay(100);  // 100 ms is sufficient for sensors to power up

  // --- Initialize I2C sensors ---
  Wire.begin(21, 22);  // SDA, SCL
  bmp.begin();
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  // --- Initialize DHT11 sensor ---
  dht.begin();

  // --- Initialize ePaper display (takes ~10 seconds) ---
  display.init(115200, true, 50, false);
  display.setRotation(1);
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  // --- First sensor read and display update ---
  updateDisplay();

  // --- Turn off sensors to save power ---
  digitalWrite(SENSORS_VCC, LOW);

  // --- Deep sleep for 5 minutes ---
  esp_sleep_enable_timer_wakeup(INTERVAL_MINUTES * 60 * 1000000ULL);  // interval in µs
  esp_deep_sleep_start();
}

// ================== Loop ==================
void loop() {
  // Empty: all work done in setup() before deep sleep
}

// ================== Display ==================
void updateDisplay() {
  // --- Read sensors ---
  float temp = NAN;
  float hum = NAN;
  int retries = 0;
  const int maxRetries = 5;

  while ((isnan(temp) || isnan(hum)) && retries < maxRetries) {
    temp = dht.readTemperature();
    hum = dht.readHumidity();
    if (isnan(temp) || isnan(hum)) delay(500);  // wait before retry
    retries++;
  }

  bool validDHT = !isnan(temp) && !isnan(hum);

  float pressure = NAN;
  float lux = NAN;

  if (validDHT) {
    pressure = bmp.readPressure() / 100.0;
    lux = lightMeter.readLightLevel();
  }

  if (validDHT) {
    if (!historyValid) {
      for (int i = 0; i < MAX_POINTS; i++) {
        tempHistory[i] = temp;
        humHistory[i] = hum;
        pressureHistory[i] = pressure;
        luxHistory[i] = lux;
      }
      historyValid = true;
    }

    tempHistory[historyIndex] = temp;
    humHistory[historyIndex] = hum;
    pressureHistory[historyIndex] = pressure;
    luxHistory[historyIndex] = lux;
    historyIndex = (historyIndex + 1) % MAX_POINTS;
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    int x = 2;
    int y = 3;
    int boxH = 25;
    int spacing = 5;
    int boxW = 190;  // leave right side for trend lines

    // --- TEMP box ---
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Temp: ");
    display.print(temp, 1);
    display.print(" C");

    // --- HUMIDITY box ---
    y += boxH + spacing;
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Hum: ");
    display.print(hum, 0);
    display.print(" %");

    // --- PRESSURE box ---
    y += boxH + spacing;
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Pressure: ");
    display.print(pressure, 1);
    display.print(" hPa");

    // --- LIGHT box ---
    y += boxH + spacing;
    display.drawRect(x, y, boxW, boxH, GxEPD_BLACK);
    display.setCursor(x + 2, y + 18);
    display.print("Light: ");
    display.print(lux, 0);
    display.print(" lux");

    // --- Draw trend lines with min/max markers ---
    int trendX = boxW + 10;  // start right of boxes
    int trendW = 55;         // width of trend
    int trendH = 20;         // trend height

    // TEMP trend
    int trendY = 3 + boxH / 2 - trendH / 2;
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
  // --- Find min/max ---
  float minVal = data[0];
  float maxVal = data[0];

  for (int i = 1; i < count; i++) {
    float v = data[i];
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }

  // Avoid divide-by-zero
  if (minVal == maxVal) maxVal = minVal + 1.0f;

  // --- Precompute scale factor (ONLY float division here) ---
  float scale = (float)h / (maxVal - minVal);

  // --- Draw trend line ---
  int lastX = x;
  int lastY = y + h - (int)((data[0] - minVal) * scale);

  for (int i = 1; i < count; i++) {
    int px = x + (i * w) / (count - 1);
    int py = y + h - (int)((data[i] - minVal) * scale);

    display.drawLine(lastX, lastY, px, py, GxEPD_BLACK);

    lastX = px;
    lastY = py;
  }

  // --- Draw min/max markers ---
  display.drawFastHLine(x, y + h, w, GxEPD_BLACK);  // min marker
  display.drawFastHLine(x, y, w, GxEPD_BLACK);      // max marker
}
