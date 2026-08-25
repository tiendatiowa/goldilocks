#include <WiFiS3.h>
#include <Wire.h>
#include <DFRobot_RGBLCD1602.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>

// =============================================================
// 1. CONFIGURATION & CONSTANTS
// =============================================================
const float SENSOR_OFFSET_CM = 35.56; // 14 inches mounting height

// Goldilocks Target Ranges
const float SAL_MIN   = 15.0, SAL_MAX   = 30.0; // ppt
const float TEMP_MIN  = 10.0, TEMP_MAX  = 25.0; // °C
const float DEPTH_MIN = 10.0, DEPTH_MAX = 80.0; // cm

// Pin Definitions
#define RGB_PIN 3       
#define ONE_WIRE_BUS 5  
#define TRIG_PIN 6      
#define ECHO_PIN 7      
#define EC_PIN A0       

// Hardware Instances
Adafruit_NeoPixel rgbLed(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
DFRobot_RGBLCD1602 lcd(0x6B, 16, 2); 
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
WiFiServer server(80);

// Global Variables
float tempC = 0.0;
float calculatedDepthCm = 0.0;
float estimatedSalinity = 0.0;

bool salinityOK = false;
bool tempOK = false;
bool depthOK = false;
bool isGoldilocks = false;
int passedCount = 0;

String statusLine2 = "";

// Timing Controls
unsigned long lastSensorRead = 0;
unsigned long lastPageSwitch = 0;
int lcdPage = 0;

// Function Declarations
void readSensors();
void evaluateHabitat();
void updateStatusLED();
void updateLCDScreen();
void handleWebDashboard();

// =============================================================
// 2. SETUP (Engine Start)
// =============================================================
void setup() {
  Serial.begin(9600);

  rgbLed.begin();
  rgbLed.setBrightness(120);
  rgbLed.show();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  tempSensor.begin();

  lcd.init();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Goldilocks Pod");
  lcd.setCursor(0, 1);
  lcd.print("Starting WiFi...");

  WiFi.beginAP("Goldilocks-Pod");
  server.begin();

  lcd.setCursor(0, 1);
  lcd.print("IP: 192.168.4.1 ");
  delay(2000);
  lcd.clear();
}

// =============================================================
// 3. MAIN LOOP (The Conductor)
// =============================================================
void loop() {
  unsigned long currentMillis = millis();

  // Run Sensor & Logic Modules every 1 second
  if (currentMillis - lastSensorRead >= 1000) {
    lastSensorRead = currentMillis;
    readSensors();
    evaluateHabitat();
    updateStatusLED();
  }

  // Run Display Module every 2 seconds
  if (currentMillis - lastPageSwitch >= 2000) {
    lastPageSwitch = currentMillis;
    updateLCDScreen();
  }

  // Always listen for phone connections
  handleWebDashboard();
}

// =============================================================
// 4. CODE MODULES
// =============================================================

// MODULE 1: Read all 3 physical sensors (Floating Pod Mode)
void readSensors() {
  // 1. Read Temperature (°C)
  tempSensor.requestTemperatures();
  tempC = tempSensor.getTempCByIndex(0);

  // 2. Read Depth with 5-Sample Median Filter
  float samples[5];
  int validCount = 0;

  for (int i = 0; i < 5; i++) {
    pinMode(ECHO_PIN, OUTPUT);
    digitalWrite(ECHO_PIN, LOW);
    delayMicroseconds(10);
    pinMode(ECHO_PIN, INPUT);

    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(30);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 35000);
    float dist = (duration > 0) ? (duration * 0.034 / 2.0) : 0;

    // Filter valid range: 30cm (sensor blind zone) up to 250cm (2.5m depth)
    if (dist >= 30.0 && dist <= 250.0) {
      samples[validCount] = dist;
      validCount++;
    }
    delay(30); // Small pause between ultrasonic bursts
  }

  // Sort valid readings to select the median value
  float rawDistanceCm = 0.0;
  if (validCount > 0) {
    for (int i = 0; i < validCount - 1; i++) {
      for (int j = i + 1; j < validCount; j++) {
        if (samples[i] > samples[j]) {
          float temp = samples[i];
          samples[i] = samples[j];
          samples[j] = temp;
        }
      }
    }
    rawDistanceCm = samples[validCount / 2]; // Select median sample
  }

  // Calculate Water Depth for Floating Setup
  if (rawDistanceCm >= SENSOR_OFFSET_CM) {
    calculatedDepthCm = rawDistanceCm - SENSOR_OFFSET_CM;
  } else {
    calculatedDepthCm = 0.0; // Dry / On land / Below offset
  }

  // 3. Read Salinity (ppt)
  int rawEC = analogRead(EC_PIN);
  estimatedSalinity = map(rawEC, 0, 1023, 0, 40);
}

// MODULE 2: Evaluate readings against Goldilocks criteria
void evaluateHabitat() {
  salinityOK = (estimatedSalinity >= SAL_MIN && estimatedSalinity <= SAL_MAX);
  tempOK     = (tempC >= TEMP_MIN && tempC <= TEMP_MAX);
  depthOK    = (calculatedDepthCm >= DEPTH_MIN && calculatedDepthCm <= DEPTH_MAX);

  isGoldilocks = salinityOK && tempOK && depthOK;
  passedCount  = (salinityOK ? 1 : 0) + (tempOK ? 1 : 0) + (depthOK ? 1 : 0);

  // Build Status String
  if (isGoldilocks) {
    statusLine2 = "GOLDILOCKS ZONE";
  } else if (passedCount == 0) {
    statusLine2 = "CRITICAL FAIL!";
  } else {
    statusLine2 = "WARN: ";
    bool first = true;
    if (!salinityOK) { statusLine2 += "SAL"; first = false; }
    if (!tempOK)     { if (!first) statusLine2 += "/"; statusLine2 += "TEMP"; first = false; }
    if (!depthOK)    { if (!first) statusLine2 += "/"; statusLine2 += "DEPTH"; }
  }
}

// MODULE 3: Change RGB light color
void updateStatusLED() {
  if (isGoldilocks) {
    rgbLed.setPixelColor(0, rgbLed.Color(0, 255, 0));   // Green
  } else if (passedCount > 0) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 180, 0)); // Yellow
  } else {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));   // Red
  }
  rgbLed.show();
}

// MODULE 4: Update physical LCD screen
void updateLCDScreen() {
  lcdPage = (lcdPage + 1) % 3;

  // Line 1: Alternate parameter views
  lcd.setCursor(0, 0);
  String line1 = "";
  if (lcdPage == 0)      line1 = "Sal: " + String(estimatedSalinity, 1) + " ppt";
  else if (lcdPage == 1) line1 = "Temp: " + String(tempC, 1) + " C";
  else                   line1 = "Depth: " + String(calculatedDepthCm, 1) + " cm";

  while (line1.length() < 16) line1 += " ";
  lcd.print(line1.substring(0, 16));

  // Line 2: Fixed Status
  lcd.setCursor(0, 1);
  String line2Formatted = statusLine2;
  while (line2Formatted.length() < 16) line2Formatted += " ";
  lcd.print(line2Formatted.substring(0, 16));
}

// MODULE 5: Serve phone dashboard over Wi-Fi
void handleWebDashboard() {
  WiFiClient client = server.available();
  if (!client) return;

  boolean currentLineIsBlank = true;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n' && currentLineIsBlank) {
        // Send HTTP Headers
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println("Connection: close");
        client.println();

        String statusBg    = isGoldilocks ? "#28a745" : (passedCount == 0 ? "#dc3545" : "#ffc107");
        String statusColor = (passedCount > 0 && !isGoldilocks) ? "#000000" : "#ffffff";

        // HTML Markup
        client.println("<!DOCTYPE html><html><head>");
        client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
        client.println("<meta http-equiv='refresh' content='3'>");
        client.println("<style>");
        client.println("body { font-family: Arial; text-align: center; background: #eef2f5; margin:0; padding:20px; }");
        client.println(".card { background: white; padding: 20px; border-radius: 12px; max-width: 400px; margin: auto; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }");
        client.println(".badge { padding: 12px; border-radius: 8px; font-weight: bold; font-size: 18px; margin-bottom: 20px; }");
        client.println(".row { display: flex; justify-content: space-between; align-items: center; padding: 12px 0; border-bottom: 1px solid #eee; font-size: 16px; }");
        client.println(".label { text-align: left; }");
        client.println(".subtext { font-size: 12px; color: #777; display: block; }");
        client.println("</style></head><body>");
        
        client.println("<div class='card'>");
        client.println("<h2>Goldilocks Pod</h2>");
        
        // Status Badge
        client.print("<div class='badge' style='background:");
        client.print(statusBg);
        client.print("; color:");
        client.print(statusColor);
        client.print(";'>");
        client.print(statusLine2);
        client.println("</div>");

        // Salinity Row
        client.println("<div class='row'><div class='label'><b>Salinity</b>");
        client.print("<span class='subtext'>Target: ");
        client.print(SAL_MIN, 1);
        client.print(" - ");
        client.print(SAL_MAX, 1);
        client.println(" ppt</span></div>");
        client.print("<b style='font-size:18px; color:");
        client.print(salinityOK ? "#28a745" : "#dc3545");
        client.print(";'>");
        client.print(estimatedSalinity, 1);
        client.println(" ppt</b></div>");

        // Temperature Row
        client.println("<div class='row'><div class='label'><b>Temperature</b>");
        client.print("<span class='subtext'>Target: ");
        client.print(TEMP_MIN, 1);
        client.print(" - ");
        client.print(TEMP_MAX, 1);
        client.println(" &deg;C</span></div>");
        client.print("<b style='font-size:18px; color:");
        client.print(tempOK ? "#28a745" : "#dc3545");
        client.print(";'>");
        client.print(tempC, 1);
        client.println(" &deg;C</b></div>");

        // Depth Row
        client.println("<div class='row'><div class='label'><b>Depth</b>");
        client.print("<span class='subtext'>Target: ");
        client.print(DEPTH_MIN, 1);
        client.print(" - ");
        client.print(DEPTH_MAX, 1);
        client.println(" cm</span></div>");
        client.print("<b style='font-size:18px; color:");
        client.print(depthOK ? "#28a745" : "#dc3545");
        client.print(";'>");
        client.print(calculatedDepthCm, 1);
        client.println(" cm</b></div>");

        client.println("</div></body></html>");
        break;
      }
      if (c == '\n') currentLineIsBlank = true;
      else if (c != '\r') currentLineIsBlank = false;
    }
  }
  delay(1);
  client.stop();
}
