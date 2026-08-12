#include <WiFiS3.h>
#include <Wire.h>
#include <DFRobot_RGBLCD1602.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>

// Pin Definitions
#define RGB_PIN 3          // DFR0605 RGB Module
#define ONE_WIRE_BUS 5     // Temperature Probe Pin
#define TRIG_PIN 6         // Ultrasonic Trig Pin (RX on SEN0208)
#define ECHO_PIN 7         // Ultrasonic Echo Pin (TX on SEN0208)
#define EC_PIN A0          // Salinity Sensor Pin

// Hardware Setup
Adafruit_NeoPixel rgbLed(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
DFRobot_RGBLCD1602 lcd(0x6B, 16, 2); // Change to 0x60 if screen remains blank
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// Wi-Fi Web Server Setup (Port 80)
WiFiServer server(80);

// Global Variables
float tempC = 0.0;
float distanceCm = 0.0;
float estimatedSalinity = 0.0;
bool salinityOK = false;
bool tempOK = false;
bool depthOK = false;
bool isGoldilocks = false;
int passedCount = 0;
String statusLine2 = "";

// Non-blocking Timing Variables
unsigned long lastSensorRead = 0;
unsigned long lastPageSwitch = 0;
int lcdPage = 0;

void setup() {
  Serial.begin(9600);

  // Initialize RGB LED
  rgbLed.begin();
  rgbLed.setBrightness(120);
  rgbLed.show();

  // Initialize Sensor Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Initialize Temperature Probe
  tempSensor.begin();

  // Initialize LCD Display
  lcd.init();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Goldilocks Pod");
  lcd.setCursor(0, 1);
  lcd.print("Starting WiFi...");

  // Start Access Point (AP Mode)
  WiFi.beginAP("Goldilocks-Pod");
  server.begin();

  lcd.setCursor(0, 1);
  lcd.print("IP: 192.168.4.1 ");
  delay(2000);
  lcd.clear();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. READ SENSORS & EVALUATE (Every 1 Second)
  if (currentMillis - lastSensorRead >= 1000) {
    lastSensorRead = currentMillis;

    // Read Temperature (°C)
    tempSensor.requestTemperatures();
    tempC = tempSensor.getTempCByIndex(0);

    // Read Weather-proof SEN0208 Depth (cm)
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
    distanceCm = (duration > 0) ? (duration * 0.034 / 2.0) : 0;

    // Read Salinity (ppt approximation)
    // int rawEC = analogRead(EC_PIN);
    // estimatedSalinity = map(rawEC, 0, 1023, 0, 40); 
    estimatedSalinity = 20.4;

    // Evaluate Thresholds
    salinityOK = (estimatedSalinity >= 15.0 && estimatedSalinity <= 30.0);
    tempOK     = (tempC >= 10.0 && tempC <= 25.0);
    depthOK    = (distanceCm >= 10.0 && distanceCm <= 80.0);

    isGoldilocks = salinityOK && tempOK && depthOK;
    passedCount  = (salinityOK ? 1 : 0) + (tempOK ? 1 : 0) + (depthOK ? 1 : 0);

    // Build Line 2 Status Message (Shared between LCD and Web)
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

    // Update Status RGB LED
    if (isGoldilocks) {
      rgbLed.setPixelColor(0, rgbLed.Color(0, 255, 0));   // Green
    } else if (passedCount > 0) {
      rgbLed.setPixelColor(0, rgbLed.Color(255, 180, 0)); // Yellow
    } else {
      rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));   // Red
    }
    rgbLed.show();
  }

  // 2. UPDATE LCD SCREEN (Alternate Line 1 Every 2 Seconds)
  if (currentMillis - lastPageSwitch >= 2000) {
    lastPageSwitch = currentMillis;
    lcdPage = (lcdPage + 1) % 3;

    // Line 1 Page Rotation
    lcd.setCursor(0, 0);
    String line1 = "";
    if (lcdPage == 0)      line1 = "Sal: " + String(estimatedSalinity, 1) + " ppt";
    else if (lcdPage == 1) line1 = "Temp: " + String(tempC, 1) + " C";
    else                   line1 = "Depth: " + String(distanceCm, 1) + " cm";

    while (line1.length() < 16) line1 += " ";
    lcd.print(line1.substring(0, 16));

    // Line 2 Status Display (Padded to 16 chars for LCD)
    lcd.setCursor(0, 1);
    String line2Formatted = statusLine2;
    while (line2Formatted.length() < 16) line2Formatted += " ";
    lcd.print(line2Formatted.substring(0, 16));
  }

  // 3. HANDLE INCOMING WI-FI CLIENTS (Phone/Tablet Dashboard)
  WiFiClient client = server.available();
  if (client) {
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n' && currentLineIsBlank) {
          // Send HTTP Header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();

          // Status Badge Colors
          String statusBg   = isGoldilocks ? "#28a745" : (passedCount == 0 ? "#dc3545" : "#ffc107");
          String statusColor= (passedCount > 0 && !isGoldilocks) ? "#000000" : "#ffffff";

          // Send HTML Webpage
          client.println("<!DOCTYPE html><html><head>");
          client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
          client.println("<meta http-equiv='refresh' content='3'>"); // Auto-refresh every 3s
          client.println("<style>");
          client.println("body { font-family: Arial; text-align: center; background: #eef2f5; margin:0; padding:20px; }");
          client.println(".card { background: white; padding: 20px; border-radius: 12px; max-width: 380px; margin: auto; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }");
          client.println(".badge { padding: 12px; border-radius: 8px; font-weight: bold; font-size: 18px; margin-bottom: 20px; }");
          client.println(".row { display: flex; justify-content: space-between; padding: 10px 0; border-bottom: 1px solid #eee; font-size: 18px; }");
          client.println("</style></head><body>");
          
          client.println("<div class='card'>");
          client.println("<h2>Goldilocks Pod</h2>");
          client.print("<div class='badge' style='background:");
          client.print(statusBg);
          client.print("; color:");
          client.print(statusColor);
          client.print(";'>");
          client.print(statusLine2); // Displays exact string from Line 2
          client.println("</div>");

          client.print("<div class='row'><span>Salinity:</span><b>");
          client.print(estimatedSalinity, 1);
          client.println(" ppt</b></div>");

          client.print("<div class='row'><span>Temperature:</span><b>");
          client.print(tempC, 1);
          client.println(" &deg;C</b></div>");

          client.print("<div class='row'><span>Depth:</span><b>");
          client.print(distanceCm, 1);
          client.println(" cm</b></div>");

          client.println("</div></body></html>");
          break;
        }
        if (c == '\n') currentLineIsBlank = true;
        else if (c != '\r') currentLineIsBlank = false;
      }
    }
    delay(1);
    client.stop(); // Close connection
  }
}
