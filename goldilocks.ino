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

// Setup DFR0605 RGB Status Light
Adafruit_NeoPixel rgbLed(1, RGB_PIN, NEO_GRB + NEO_KHZ800);

// Setup DFRobot Gravity LCD1602 Display (I2C address 0x6B for V1.1 / 0x60 for V1.0)
DFRobot_RGBLCD1602 lcd(0x6B, 16, 2); 

// Setup OneWire for Temperature Probe
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

void setup() {
  Serial.begin(9600);

  // Initialize RGB LED
  rgbLed.begin();
  rgbLed.setBrightness(120);
  rgbLed.show();

  // Initialize Sensor Pins
  // pinMode(TRIG_PIN, OUTPUT);
  // pinMode(ECHO_PIN, INPUT);

  // Initialize Temperature Probe
  tempSensor.begin();

  // Initialize DFRobot LCD Module
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
  // 1. Read Temperature (°C)
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);

  // 2. Read Depth/Distance (cm)
  // digitalWrite(TRIG_PIN, LOW);
  // delayMicroseconds(2);
  // digitalWrite(TRIG_PIN, HIGH);
  // delayMicroseconds(10);
  // digitalWrite(TRIG_PIN, LOW);
  // long duration = pulseIn(ECHO_PIN, HIGH);
  // float distanceCm = duration * 0.034 / 2;
  float distanceCm = 30.0;

  // 3. Read Salinity/EC
  // int rawEC = analogRead(EC_PIN);
  // float estimatedSalinity = map(rawEC, 0, 1023, 0, 40); 
  float estimatedSalinity = 20.0;

  // 4. Multi-Variable Goldilocks Assessment
  bool salinityOK  = (estimatedSalinity >= 15.0 && estimatedSalinity <= 30.0);
  bool tempOK      = (tempC >= 10.0 && tempC <= 25.0);
  bool depthOK     = (distanceCm >= 10.0 && distanceCm <= 80.0);

  bool isGoldilocks = salinityOK && tempOK && depthOK;
  int passedCount = (salinityOK ? 1 : 0) + (tempOK ? 1 : 0) + (depthOK ? 1 : 0);
  bool isWarning = (passedCount >= 1 && passedCount < 3);

  // Update Status LED
  if (isGoldilocks) {
    rgbLed.setPixelColor(0, rgbLed.Color(0, 255, 0));   // Green
  } else if (isWarning) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 180, 0)); // Yellow
  } else {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));   // Red
  }
  rgbLed.show();

  // 5. Update Screen
  lcd.setCursor(0, 0);
  lcd.print("S:");
  lcd.print((int)estimatedSalinity);
  lcd.print(" T:");
  lcd.print((int)tempC);
  lcd.print(" D:");
  lcd.print((int)distanceCm);
  lcd.print("  ");

  lcd.setCursor(0, 1);
  String line2 = "";
  if (isGoldilocks) {
    line2 = "STAT: GOLDILOCKS";
  } else {
    line2 = "FAIL: ";
    if (!salinityOK) line2 += "SAL ";
    if (!tempOK)     line2 += "TMP ";
    if (!depthOK)    line2 += "DEP ";

    // Fill the rest of the line with spaces up to 16 characters
    while (line2.length() < 16) {
      line2 += " ";
    }
    // Truncate if it exceeds 16 characters
    if (line2.length() > 16) {
      line2 = line2.substring(0, 16);
    }

  }

  lcd.print(line2);

  delay(1000);
}
