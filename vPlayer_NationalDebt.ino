#include <WiFi.h>
#include <HTTPClient.h>
#include "Arduino_GFX_Library.h"
#include "icelandFont.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include <time.h>
#include "esp_ota_ops.h"

// SDMMC pin configuration
int sdMMC_CMD = 35;
int sdMMC_CLK = 36;
int sdMMC_D0 = 37;
int sdMMC_D1 = 38;
int sdMMC_D2 = 33;
int sdMMC_D3 = 34;

// Declare the TFT display object globally
Arduino_DataBus *bus = new Arduino_HWSPI(13 /* DC */, 10 /* CS */, 12 /* SCK */, 11 /* MOSI */, 11 /* MISO */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, 14 /* RST */, 1 /* rotation */, true /* IPS */, 240 /* width */, 280 /* height */, 0 /* col offset */, 276 /* row offset */);
#define TFT_BL 9

String ssid = "";
String password = "";
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 24 * 60 * 60 * 1000;  // Fetch new data every 24 hours
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 200;  // Update every 200ms
long long currentDebt = 0;
long long previousDebt = 0;
long long incrementPerUpdate = 0;

void setup() {
  Serial.begin(115200);

  // Set up the TFT display
  ledcSetup(1, 12000, 8);
  ledcAttachPin(TFT_BL, 1);
  ledcWrite(1, 255);

  gfx->begin(60000000);
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);
  gfx->setFont(&Iceland_Regular20pt7b);

  if (!SD_MMC.setPins(sdMMC_CLK, sdMMC_CMD, sdMMC_D0, sdMMC_D1, sdMMC_D2, sdMMC_D3)) {
    Serial.println(F("ERROR: Failed to set custom SDMMC pins!"));
    return;
  }

  if (!SD_MMC.begin()) {
    gfx->setCursor(0, 0);
    gfx->println("SD Card Mount Failed");
    return;
  }
  checkAndUpdateFirmware();  // Check for firmware update at boot

  if (!loadSettings()) {
    createDefaultSettings();
    gfx->setCursor(5, 10);
    gfx->println("Waiting for settings");
    gfx->println("Check SD card");
    while (true) {
      delay(1000);
    }
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    gfx->setCursor(0, 0);
    gfx->println("WiFi Connect Failed");
    return;
  }

  gfx->fillScreen(BLACK);
  fetchAndDisplayNationalDebt();
}

void loop() {
  unsigned long now = millis();

  // Fetch new data once every 24 hours
  if (now - lastFetchTime >= fetchInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      fetchAndDisplayNationalDebt();
      lastFetchTime = now;
    }
  }

  // Smoothly update the debt display
  if (now - lastUpdateTime >= updateInterval && previousDebt != currentDebt) {
    // Increment or decrement toward the currentDebt
    if (previousDebt < currentDebt) {
      previousDebt += incrementPerUpdate;

      // Ensure it doesn't exceed the current debt
      if (previousDebt > currentDebt) {
        previousDebt = currentDebt;
      }
    } else {
      previousDebt -= incrementPerUpdate;

      // Ensure it doesn't go below the current debt
      if (previousDebt < currentDebt) {
        previousDebt = currentDebt;
      }
    }

    renderDebt(previousDebt);
    lastUpdateTime = now;
  }
}

bool loadSettings() {
  File file = SD_MMC.open("/settings.txt");
  if (!file) {
    return false;
  }

  ssid = file.readStringUntil('\n');
  ssid.trim();
  password = file.readStringUntil('\n');
  password.trim();

  file.close();
  return true;
}

void createDefaultSettings() {
  File file = SD_MMC.open("/settings.txt", FILE_WRITE);
  if (!file) {
    return;
  }

  file.println("YOUR_SSID");
  file.println("YOUR_PASSWORD");

  file.close();
}

void fetchAndDisplayNationalDebt() {
  HTTPClient http;
  String apiUrl = "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/debt_to_penny?sort=-record_date&limit=2";

  http.begin(apiUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      gfx->fillRect(0, 30, gfx->width(), gfx->height() - 30, BLACK);
      gfx->setCursor(0, 30);
      gfx->println("JSON Parse Error");
      Serial.println("JSON Parse Error");
      return;
    }

    // Parse API values as dollars
    String currentDebtStr = doc["data"][0]["tot_pub_debt_out_amt"].as<String>();
    String previousDebtStr = doc["data"][1]["tot_pub_debt_out_amt"].as<String>();

    currentDebt = static_cast<long long>(currentDebtStr.toDouble());  // Already in dollars
    previousDebt = static_cast<long long>(previousDebtStr.toDouble());

    // Debug output
    Serial.println("Parsed Values:");
    Serial.print("Current Debt: ");
    Serial.println(currentDebt);
    Serial.print("Previous Debt: ");
    Serial.println(previousDebt);


    // Calculate increment per update
    long long totalDifference = abs(currentDebt - previousDebt);
    incrementPerUpdate = totalDifference / (fetchInterval / updateInterval);

    if (incrementPerUpdate < 1) {
      incrementPerUpdate = 1;  // Ensure minimum increment
    }


    Serial.print("Total Difference (Dollars): ");
    Serial.println(totalDifference);
    Serial.print("Increment Per Update (Dollars): ");
    Serial.println(incrementPerUpdate);

    renderDebt(previousDebt);  // Start displaying the previous debt
  } else {
    gfx->fillRect(0, 30, gfx->width(), gfx->height() - 30, BLACK);
    gfx->setCursor(0, 30);
    gfx->println("API Request Failed");
    Serial.println("API Request Failed");
  }

  http.end();
}

void renderDebt(long long debtInDollars) {
  // Work directly in dollars
  long trillions = debtInDollars / 1000000000000;
  debtInDollars %= 1000000000000;

  long billions = debtInDollars / 1000000000;
  debtInDollars %= 1000000000;

  long millions = debtInDollars / 1000000;
  debtInDollars %= 1000000;

  long thousands = debtInDollars / 1000;
  long dollars = debtInDollars % 1000;
  // Define a list of light colors for cycling
  const uint16_t lightColors[] = {
    RGB565_LIGHTGREY,
    RGB565_CYAN,
    RGB565_MAGENTA,
    RGB565_YELLOW,
    RGB565_ORANGE,
    RGB565_GREENYELLOW,
    RGB565_WHITE
  };
  const int numColors = sizeof(lightColors) / sizeof(lightColors[0]);  // Number of colors

  // Declare static variables to track previous values and color index
  static long prevTrillions = -1;
  static long prevBillions = -1;
  static long prevMillions = -1;
  static long prevThousands = -1;
  static long prevDollars = -1;

  static int trillionsColorIndex = 0;
  static int billionsColorIndex = 0;
  static int millionsColorIndex = 0;
  static int thousandsColorIndex = 0;
  static int dollarsColorIndex = 0;

  // Check and update "trillions"
  if (trillions != prevTrillions) {
    trillionsColorIndex = (trillionsColorIndex + 1) % numColors;
    gfx->setTextColor(lightColors[trillionsColorIndex], BLACK);
    gfx->fillRect(0, 40 - 30, gfx->width(), 32, BLACK);
    gfx->setCursor(0, 40);
    gfx->printf("   %ld Trillion", trillions);
    prevTrillions = trillions;
  }

  // Check and update "billions"
  if (billions != prevBillions) {
    billionsColorIndex = (billionsColorIndex + 1) % numColors;
    gfx->setTextColor(lightColors[billionsColorIndex], BLACK);
    gfx->fillRect(0, 80 - 30, gfx->width(), 32, BLACK);
    gfx->setCursor(0, 80);
    gfx->printf("   %ld Billion", billions);
    prevBillions = billions;
  }

  // Check and update "millions"
  if (millions != prevMillions) {
    millionsColorIndex = (millionsColorIndex + 1) % numColors;
    gfx->setTextColor(lightColors[millionsColorIndex], BLACK);
    gfx->fillRect(0, 120 - 30, gfx->width(), 32, BLACK);
    gfx->setCursor(0, 120);
    gfx->printf("   %ld Million", millions);
    prevMillions = millions;
  }

  // Check and update "thousands"
  if (thousands != prevThousands) {
    thousandsColorIndex = (thousandsColorIndex + 1) % numColors;
    gfx->setTextColor(lightColors[thousandsColorIndex], BLACK);
    gfx->fillRect(0, 160 - 30, gfx->width(), 32, BLACK);
    gfx->setCursor(0, 160);
    gfx->printf("   %ld Thousand", thousands);
    prevThousands = thousands;
  }

  // Check and update "dollars"
  if (dollars != prevDollars) {
    dollarsColorIndex = (dollarsColorIndex + 1) % numColors;
    gfx->setTextColor(lightColors[dollarsColorIndex], BLACK);
    gfx->fillRect(0, 200 - 30, gfx->width(), 32, BLACK);
    gfx->setCursor(0, 200);
    gfx->printf("   %ld Dollars", dollars);
    prevDollars = dollars;
  }
}
