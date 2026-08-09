#include <Wire.h>
#include <DFRobot_RGBLCD1602.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>

// Pin Definitions
#define RGB_PIN 3          // DFR0605 RGB Module
#define ONE_WIRE_BUS 5     // Temperature Probe Pin
// #define TRIG_PIN 6         // Ultrasonic Trig Pin
// #define ECHO_PIN 7         // Ultrasonic Echo Pin
// #define EC_PIN A0          // Salinity Sensor Pin

// Hardware Setup
Adafruit_NeoPixel rgbLed(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
DFRobot_RGBLCD1602 lcd(0x6B, 16, 2); // Change to 0x60 if screen remains blank
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

void setup() {
  Serial.begin(9600);

  rgbLed.begin();
  rgbLed.setBrightness(120);
  rgbLed.show();

  // pinMode(TRIG_PIN, OUTPUT);
  // pinMode(ECHO_PIN, INPUT);

  tempSensor.begin();

  lcd.init();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Goldilocks Pod");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();
}

void loop() {
  // 1. Read Sensors
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);

  // digitalWrite(TRIG_PIN, LOW);
  // delayMicroseconds(2);
  // digitalWrite(TRIG_PIN, HIGH);
  // delayMicroseconds(10);
  // digitalWrite(TRIG_PIN, LOW);
  // long duration = pulseIn(ECHO_PIN, HIGH);
  // float distanceCm = duration * 0.034 / 2;
  float distanceCm = 80.4;

  // int rawEC = analogRead(EC_PIN);
  // float estimatedSalinity = map(rawEC, 0, 1023, 0, 40); 
  float estimatedSalinity = 44.5;

  // 2. Evaluate Goldilocks Thresholds
  bool salinityOK  = (estimatedSalinity >= 15.0 && estimatedSalinity <= 30.0);
  bool tempOK      = (tempC >= 10.0 && tempC <= 25.0);
  bool depthOK     = (distanceCm >= 10.0 && distanceCm <= 80.0);

  bool isGoldilocks = salinityOK && tempOK && depthOK;
  int passedCount = (salinityOK ? 1 : 0) + (tempOK ? 1 : 0) + (depthOK ? 1 : 0);

  // 3. Update Status RGB LED
  if (isGoldilocks) {
    rgbLed.setPixelColor(0, rgbLed.Color(0, 255, 0));   // Green
  } else if (passedCount > 0) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 180, 0)); // Yellow
  } else {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));   // Red (All fail)
  }
  rgbLed.show();

  // 4. Build Line 2 Status Message
  String line2 = "";
  if (isGoldilocks) {
    line2 = "GOLDILOCKS ZONE";
  } else if (passedCount == 0) {
    line2 = "ALL FAIL!";
  } else {
    line2 = "WARN: ";
    bool first = true;
    if (!salinityOK) { line2 += "SAL"; first = false; }
    if (!tempOK)     { if (!first) line2 += "/"; line2 += "TEMP"; first = false; }
    if (!depthOK)    { if (!first) line2 += "/"; line2 += "DEPTH"; }
  }

  // Pad Line 2 to exactly 16 characters
  while (line2.length() < 16) {
    line2 += " ";
  }
  line2 = line2.substring(0, 16);

  // 5. Prepare Line 1 Pages (Clean 1-Decimal Formatting)
  String page1 = "Sal: " + String(estimatedSalinity, 1) + " ppt";
  String page2 = "Temp: " + String(tempC, 1) + " C";
  String page3 = "Depth: " + String(distanceCm, 1) + " cm";

  // Pad each page to 16 characters
  while (page1.length() < 16) page1 += " ";
  while (page2.length() < 16) page2 += " ";
  while (page3.length() < 16) page3 += " ";

  // Display Line 2 Status (remains visible)
  lcd.setCursor(0, 1);
  lcd.print(line2);

  // Cycle Through Line 1 Pages (2 seconds per page)
  
  // Page 1: Salinity
  lcd.setCursor(0, 0);
  lcd.print(page1.substring(0, 16));
  delay(2000);

  // Page 2: Temperature
  lcd.setCursor(0, 0);
  lcd.print(page2.substring(0, 16));
  delay(2000);

  // Page 3: Depth
  lcd.setCursor(0, 0);
  lcd.print(page3.substring(0, 16));
  delay(2000);
}
