#include <WiFiS3.h>
#include <Wire.h>
#include <RTC.h>
#include <DFRobot_RGBLCD1602.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <SD.h>

// =============================================================
// 1. CONFIGURATION & CONSTANTS
// =============================================================
const float SENSOR_OFFSET_CM = 35.56; // Fixed 14" height above water surface

// Goldilocks Target Ranges
const float SAL_MIN   = 15.0, SAL_MAX   = 30.0; // ppt
const float TEMP_MIN  = 10.0, TEMP_MAX  = 25.0; // °C
const float DEPTH_MIN = 10.0, DEPTH_MAX = 80.0; // cm

// Data Logging Configurations
const int MAX_HISTORY = 150;     // Rolling graph history (5 mins @ 1 sample / 2 sec)
const int SD_CS_PIN   = 2;       // Verified Chip Select pin on Expansion Shield v7.1
const char* LOG_FILENAME = "data.csv";

// Pin Definitions
#define RGB_PIN 3
#define ONE_WIRE_BUS 5
#define TRIG_PIN 6
#define ECHO_PIN 7

// MicroSD Status Variables
bool sdOK = false;
int sdSampleCount = 0;

// Hardware Instances
Adafruit_NeoPixel rgbLed(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
DFRobot_RGBLCD1602 lcd(0x6B, 16, 2); 
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
WiFiServer server(80);

// Live Sensor Readings
float tempC = 0.0;
float calculatedDepthCm = 0.0;
float estimatedSalinity = 0.0;
float rs485EC_uS = 0.0;
float lastValidDepthCm = 20.0; // Default fallback depth

// Goldilocks Status
bool salinityOK = false;
bool tempOK = false;
bool depthOK = false;
bool isGoldilocks = false;
int passedCount = 0;
String statusLine2 = "";

// Rolling Time Series Memory (for live Canvas chart)
float salHistory[MAX_HISTORY];
float tempHistory[MAX_HISTORY];
float depthHistory[MAX_HISTORY];
int historyCount = 0;
bool timestampSet = false;

// Timing Controls
unsigned long lastSensorRead = 0;
unsigned long lastDataLog = 0;      // 2-second chart logger
unsigned long lastFieldLog = 0;     // 30-second logger
unsigned long lastPageSwitch = 0;
int lcdPage = 0;

// Function Declarations
void readSensors();
void readRS485Salinity();
uint16_t calculateCRC16(const uint8_t *buf, int len);
void evaluateHabitat();
void updateStatusLED();
void updateLCDScreen();
void recordDataHistory();
void recordFieldSample();
void handleWebDashboard();

// =============================================================
// 2. SETUP
// =============================================================
void setup() {
  Serial.begin(115200);

  // Initialize Hardware Serial1 for RS485 Sensor (4800 Baud, 8N1)
  Serial1.begin(4800);

  // Initialize Uno R4 Native Real-Time Clock
  RTC.begin();

  rgbLed.begin();
  rgbLed.setBrightness(120);
  rgbLed.show();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  tempSensor.begin();
  tempSensor.setWaitForConversion(false); // Async mode to prevent Wi-Fi latency
  tempSensor.requestTemperatures();       // Initial conversion trigger

  lcd.init();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Goldilocks Pod");
  lcd.setCursor(0, 1);
  lcd.print("Starting SD & AP...");

  // Set hardware SPI SS pin (Pin 10) to OUTPUT HIGH for ARM SPI bus stability
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);

  // Initialize MicroSD Card Module on Pin 2
  if (SD.begin(SD_CS_PIN)) {
    sdOK = true;
    Serial.println("SD Card Initialized Successfully on Pin 2!");

    // Create file and CSV header if file doesn't exist
    if (!SD.exists(LOG_FILENAME)) {
      File logFile = SD.open(LOG_FILENAME, FILE_WRITE);
      if (logFile) {
        logFile.println("Sample_ID,Unix_Timestamp,Time_Synced,Temperature_C,Salinity_ppt,Depth_cm");
        logFile.close();
      }
    } else {
      // Resume ID count after reboot by counting existing rows
      File logFile = SD.open(LOG_FILENAME, FILE_READ);
      if (logFile) {
        int lines = 0;
        while (logFile.available()) {
          if (logFile.read() == '\n') lines++;
        }
        logFile.close();
        if (lines > 0) sdSampleCount = lines - 1; // Exclude header line
      }
    }
  } else {
    Serial.println("SD Card Initialization Failed! Check CS Pin & Wiring.");
  }

  WiFi.beginAP("Goldilocks-Pod");
  server.begin();

  lcd.setCursor(0, 1);
  lcd.print("IP: 192.168.4.1 ");
  delay(2000);
  lcd.clear();
}

// =============================================================
// 3. MAIN LOOP
// =============================================================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Read & Evaluate Sensors (Every 1 Second)
  if (currentMillis - lastSensorRead >= 1000) {
    lastSensorRead = currentMillis;
    readSensors();
    evaluateHabitat();
    updateStatusLED();
  }

  // 2. Store Rolling Chart Data (Every 2 Seconds)
  if (currentMillis - lastDataLog >= 2000) {
    lastDataLog = currentMillis;
    recordDataHistory();
  }

  // 3. Store Long-Term Sample to MicroSD (Every 30 Seconds)
  if (currentMillis - lastFieldLog >= 30000) {
    lastFieldLog = currentMillis;
    recordFieldSample();
  }

  // 4. Rotate LCD Display Pages (Every 2 Seconds)
  if (currentMillis - lastPageSwitch >= 2000) {
    lastPageSwitch = currentMillis;
    updateLCDScreen();
  }

  // 5. Serve Wi-Fi Requests
  handleWebDashboard();
}

// =============================================================
// 4. CODE MODULES
// =============================================================

void readSensors() {
  // 1. Read Temperature (°C) - Async
  tempC = tempSensor.getTempCByIndex(0);
  tempSensor.requestTemperatures();

  // 2. Read Depth with Zero-Rejection & Acoustic Ring-Down Settling
  int numOfSamples = 3;
  float validSamples[numOfSamples];
  int validCount = 0;

  for (int i = 0; i < numOfSamples; i++) {
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

    if (dist >= 30.0 && dist <= 300.0) {
      validSamples[validCount] = dist;
      validCount++;
    }
    
    delay(70); // Acoustic ring-down delay
  }

  if (validCount > 0) {
    for (int i = 0; i < validCount - 1; i++) {
      for (int j = i + 1; j < validCount; j++) {
        if (validSamples[i] > validSamples[j]) {
          float temp = validSamples[i];
          validSamples[i] = validSamples[j];
          validSamples[j] = temp;
        }
      }
    }
    
    float medianRawDist = validSamples[validCount / 2];

    if (medianRawDist >= SENSOR_OFFSET_CM) {
      lastValidDepthCm = medianRawDist - SENSOR_OFFSET_CM;
    } else {
      lastValidDepthCm = 0.0;
    }
  } 

  calculatedDepthCm = lastValidDepthCm;

  // 3. Read RS485 Modbus EC / Salinity Sensor
  readRS485Salinity();
}

// Modbus RTU Reader for DFRobot RS485 EC Sensor (K=10, Addr: 0x01, Baud: 4800)
void readRS485Salinity() {
  while (Serial1.available()) Serial1.read(); // Clear buffer

  // Read 3 Registers starting at 0x0000 from Slave Address 0x01
  uint8_t request[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
  uint16_t crc = calculateCRC16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  Serial1.write(request, 8);

  unsigned long startWait = millis();
  while (Serial1.available() < 11 && millis() - startWait < 200) {
    delay(2);
  }

  if (Serial1.available() >= 11) {
    uint8_t response[11];
    Serial1.readBytes(response, 11);

    if (response[0] == 0x01 && response[1] == 0x03 && response[2] == 0x06) {
      uint16_t respCRC = (response[10] << 8) | response[9];
      if (calculateCRC16(response, 9) == respCRC) {
        
        // Register 0x0000: EC value (uS/cm)
        rs485EC_uS = (float)((response[3] << 8) | response[4]);

        // Register 0x0002: Salinity (ppm) -> Convert to ppt (1 ppt = 1000 ppm)
        uint16_t salinityPPM = (response[7] << 8) | response[8];
        estimatedSalinity = (float)salinityPPM / 1000.0;

        Serial.print("RS485 EC: "); Serial.print(rs485EC_uS, 0);
        Serial.print(" uS/cm | Salinity: "); Serial.print(estimatedSalinity, 2); Serial.println(" ppt");
        return;
      }
    }
  }

  Serial.println("RS485 read timeout or CRC mismatch.");
}

uint16_t calculateCRC16(const uint8_t *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void evaluateHabitat() {
  salinityOK = (estimatedSalinity >= SAL_MIN && estimatedSalinity <= SAL_MAX);
  tempOK     = (tempC >= TEMP_MIN && tempC <= TEMP_MAX);
  depthOK    = (calculatedDepthCm >= DEPTH_MIN && calculatedDepthCm <= DEPTH_MAX);

  isGoldilocks = salinityOK && tempOK && depthOK;
  passedCount  = (salinityOK ? 1 : 0) + (tempOK ? 1 : 0) + (depthOK ? 1 : 0);

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

void updateStatusLED() {
  if (isGoldilocks)      rgbLed.setPixelColor(0, rgbLed.Color(0, 255, 0));   // Green
  else if (passedCount) rgbLed.setPixelColor(0, rgbLed.Color(255, 180, 0)); // Yellow
  else                  rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0));   // Red
  rgbLed.show();
}

void recordDataHistory() {
  if (historyCount < MAX_HISTORY) {
    salHistory[historyCount]   = estimatedSalinity;
    tempHistory[historyCount]  = tempC;
    depthHistory[historyCount] = calculatedDepthCm;
    historyCount++;
  } else {
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      salHistory[i]   = salHistory[i + 1];
      tempHistory[i]  = tempHistory[i + 1];
      depthHistory[i] = depthHistory[i + 1];
    }
    salHistory[MAX_HISTORY - 1]   = estimatedSalinity;
    tempHistory[MAX_HISTORY - 1]  = tempC;
    depthHistory[MAX_HISTORY - 1] = calculatedDepthCm;
  }
}

void recordFieldSample() {
  if (!sdOK) return;

  RTCTime currentTime;
  RTC.getTime(currentTime);
  unsigned long epoch = currentTime.getUnixTime();

  File logFile = SD.open(LOG_FILENAME, FILE_WRITE);
  if (logFile) {
    sdSampleCount++;
    logFile.print(sdSampleCount);
    logFile.print(",");
    logFile.print(epoch);
    logFile.print(",");
    logFile.print(timestampSet ? "1" : "0");
    logFile.print(",");
    logFile.print(tempC, 1);
    logFile.print(",");
    logFile.print(estimatedSalinity, 1);
    logFile.print(",");
    logFile.println(calculatedDepthCm, 1);
    logFile.close();

    Serial.print("SD Logged Sample #");
    Serial.print(sdSampleCount);
    Serial.print(" | Synced: ");
    Serial.println(timestampSet ? "YES" : "NO");
  } else {
    Serial.println("Error writing to data.csv on SD card!");
  }
}

void updateLCDScreen() {
  lcdPage = (lcdPage + 1) % 3;

  lcd.setCursor(0, 0);
  String line1 = "";
  if (lcdPage == 0)      line1 = "Sal: " + String(estimatedSalinity, 1) + " ppt";
  else if (lcdPage == 1) line1 = "Temp: " + String(tempC, 1) + " C";
  else                   line1 = "Depth: " + String(calculatedDepthCm, 1) + " cm";

  while (line1.length() < 16) line1 += " ";
  lcd.print(line1.substring(0, 16));

  lcd.setCursor(0, 1);
  String line2Formatted = statusLine2;
  while (line2Formatted.length() < 16) line2Formatted += " ";
  lcd.print(line2Formatted.substring(0, 16));
}

void handleWebDashboard() {
  WiFiClient client = server.available();
  if (!client) return;

  String request = client.readStringUntil('\r');
  client.flush();

  if (request.indexOf("/settime?epoch=") != -1) {
    int startIdx = request.indexOf("epoch=") + 6;
    int endIdx = request.indexOf(" ", startIdx);
    String epochStr = request.substring(startIdx, endIdx);
    unsigned long epoch = strtoul(epochStr.c_str(), NULL, 10);

    if (epoch > 0) {
      RTCTime newTime(epoch);
      RTC.setTime(newTime);
      timestampSet = true;
      Serial.print("RTC Synced to Epoch: ");
      Serial.println(epoch);
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close\r\n");
    client.println("OK");
  }
  else if (request.indexOf("/csv") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/csv");
    client.println("Content-Disposition: attachment; filename=\"goldilocks_sd_data.csv\"");
    client.println("Connection: close\r\n");

    if (SD.exists(LOG_FILENAME)) {
      File logFile = SD.open(LOG_FILENAME, FILE_READ);
      if (logFile) {
        uint8_t buffer[64];
        while (logFile.available()) {
          int bytesRead = logFile.read(buffer, sizeof(buffer));
          client.write(buffer, bytesRead);
        }
        logFile.close();
      }
    }
  }
  else if (request.indexOf("/data") != -1) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    client.print("{\"salVal\":"); client.print(estimatedSalinity, 1);
    client.print(",\"salOK\":"); client.print(salinityOK ? "true" : "false");
    client.print(",\"tempVal\":"); client.print(tempC, 1);
    client.print(",\"tempOK\":"); client.print(tempOK ? "true" : "false");
    client.print(",\"depthVal\":"); client.print(calculatedDepthCm, 1);
    client.print(",\"depthOK\":"); client.print(depthOK ? "true" : "false");
    client.print(",\"status\":\""); client.print(statusLine2); client.print("\"");
    client.print(",\"goldi\":"); client.print(isGoldilocks ? "true" : "false");
    client.print(",\"passed\":"); client.print(passedCount);
    client.print(",\"samples\":"); client.print(sdSampleCount);
    client.print(",\"synced\":"); client.print(timestampSet ? "true" : "false");

    client.print(",\"sal\":[");
    for (int i = 0; i < historyCount; i++) { client.print(salHistory[i], 1); if (i < historyCount - 1) client.print(","); }
    client.print("],\"temp\":[");
    for (int i = 0; i < historyCount; i++) { client.print(tempHistory[i], 1); if (i < historyCount - 1) client.print(","); }
    client.print("],\"depth\":[");
    for (int i = 0; i < historyCount; i++) { client.print(depthHistory[i], 1); if (i < historyCount - 1) client.print(","); }
    client.print("]}");
  } 
  else {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();

    client.println("<!DOCTYPE html><html><head>");
    client.println("<meta charset='UTF-8'>");
    client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.println("<style>");
    client.println("body { font-family: Arial; text-align: center; background: #eef2f5; margin:0; padding:15px; }");
    client.println(".card { background: white; padding: 20px; border-radius: 12px; max-width: 420px; margin: auto; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }");
    client.println(".badge { padding: 12px; border-radius: 8px; font-weight: bold; font-size: 18px; margin-bottom: 20px; transition: all 0.3s; }");
    client.println(".sync-box { display: flex; align-items: center; justify-content: space-between; background: #f8f9fa; border: 1px solid #e9ecef; border-radius: 8px; padding: 8px 12px; margin-bottom: 15px; font-size: 13px; }");
    client.println(".sync-btn { background: #007bff; color: white; border: none; padding: 6px 12px; border-radius: 6px; font-weight: bold; cursor: pointer; font-size: 12px; }");
    client.println(".sync-btn:disabled { background: #6c757d; opacity: 0.65; cursor: not-allowed; }");
    client.println(".row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid #eee; font-size: 16px; }");
    client.println(".label { text-align: left; }");
    client.println(".subtext { font-size: 12px; color: #777; display: block; }");
    client.println("canvas { width: 100%; height: 130px; background: #fafafa; border: 1px solid #ddd; border-radius: 6px; margin-top: 6px; }");
    client.println(".btn { display: block; width: 100%; box-sizing: border-box; padding: 14px; background: #28a745; color: white; text-decoration: none; border-radius: 8px; font-weight: bold; font-size: 16px; margin-top: 20px; text-align: center; }");
    client.println("</style></head><body>");
    
    client.println("<div class='card'>");
    client.println("<h2>Goldilocks Pod</h2>");
    
    client.println("<div id='badge' class='badge'>Loading...</div>");

    client.println("<div class='sync-box'>");
    client.println("<span id='syncStatus' style='color: #6c757d;'>Connecting to RTC...</span>");
    client.println("<button id='syncBtn' class='sync-btn' onclick='syncTime()' style='display: none;'>Retry Sync</button>");
    client.println("</div>");

    // Salinity
    client.println("<div class='row'><div class='label'><b>Salinity</b><span class='subtext'>Target: 15.0 - 30.0 ppt</span></div>");
    client.println("<b id='salVal' style='font-size:18px;'>-- ppt</b></div>");
    client.println("<canvas id='salChart'></canvas>");

    // Temperature
    client.println("<div class='row'><div class='label'><b>Temperature</b><span class='subtext'>Target: 10.0 - 25.0 &deg;C</span></div>");
    client.println("<b id='tempVal' style='font-size:18px;'>-- &deg;C</b></div>");
    client.println("<canvas id='tempChart'></canvas>");

    // Depth
    client.println("<div class='row'><div class='label'><b>Depth</b><span class='subtext'>Target: 10.0 - 80.0 cm</span></div>");
    client.println("<b id='depthVal' style='font-size:18px;'>-- cm</b></div>");
    client.println("<canvas id='depthChart'></canvas>");

    // CSV Download
    client.println("<a href='/csv' class='btn'>Download SD CSV (<span id='logCount'>0</span> samples)</a>");

    client.println("</div>");

    client.println("<script>");
    
    client.println("function syncTime() {");
    client.println("  const statusEl = document.getElementById('syncStatus');");
    client.println("  const btn = document.getElementById('syncBtn');");
    client.println("  statusEl.innerText = 'Syncing phone time...';");
    client.println("  statusEl.style.color = '#6c757d';");
    client.println("  btn.disabled = true;");
    
    client.println("  const epoch = Math.floor(Date.now() / 1000);");
    client.println("  fetch('/settime?epoch=' + epoch)");
    client.println("    .then(r => { if(r.ok) return r.text(); else throw new Error('Failed'); })");
    client.println("    .then(() => {");
    client.println("      statusEl.innerText = '\\u2713 Time Synced with Phone';");
    client.println("      statusEl.style.color = '#28a745';");
    client.println("      btn.style.display = 'none';");
    client.println("    })");
    client.println("    .catch(e => {");
    client.println("      statusEl.innerText = '\\u26A0 Time Unsynced';");
    client.println("      statusEl.style.color = '#dc3545';");
    client.println("      btn.style.display = 'inline-block';");
    client.println("      btn.disabled = false;");
    client.println("    });");
    client.println("}");

    client.println("syncTime();");

    client.println("function drawGraph(id, data, baseColor, minScale, maxScale, targetMin, targetMax) {");
    client.println("  const c = document.getElementById(id); if(!c) return;");
    client.println("  const ctx = c.getContext('2d');");
    client.println("  c.width = c.clientWidth; c.height = c.clientHeight;");
    client.println("  ctx.clearRect(0,0,c.width,c.height);");
    
    client.println("  let plotHeight = c.height - 20;");

    client.println("  let yMinTarget = plotHeight - ((targetMin - minScale)/(maxScale - minScale) * (plotHeight - 10) + 5);");
    client.println("  let yMaxTarget = plotHeight - ((targetMax - minScale)/(maxScale - minScale) * (plotHeight - 10) + 5);");
    client.println("  ctx.fillStyle = 'rgba(40, 167, 69, 0.08)';");
    client.println("  ctx.fillRect(0, Math.min(yMinTarget, yMaxTarget), c.width, Math.abs(yMinTarget - yMaxTarget));");

    client.println("  if(data.length < 2) return;");

    client.println("  for(let i=0; i<data.length-1; i++) {");
    client.println("    let x1 = (i / (150 - 1)) * c.width;");
    client.println("    let norm1 = (data[i] - minScale) / (maxScale - minScale);");
    client.println("    let y1 = plotHeight - (Math.max(0, Math.min(1, norm1)) * (plotHeight - 10) + 5);");
    client.println("    let x2 = ((i+1) / (150 - 1)) * c.width;");
    client.println("    let norm2 = (data[i+1] - minScale) / (maxScale - minScale);");
    client.println("    let y2 = plotHeight - (Math.max(0, Math.min(1, norm2)) * (plotHeight - 10) + 5);");
    client.println("    let isOut = (data[i] < targetMin || data[i] > targetMax || data[i+1] < targetMin || data[i+1] > targetMax);");
    client.println("    ctx.strokeStyle = isOut ? '#dc3545' : baseColor;");
    client.println("    ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(x1,y1); ctx.lineTo(x2,y2); ctx.stroke();");
    client.println("  }");

    client.println("  for(let i=0; i<data.length; i++) {");
    client.println("    if(data[i] < targetMin || data[i] > targetMax) {");
    client.println("      let x = (i / (150 - 1)) * c.width;");
    client.println("      let norm = (data[i] - minScale) / (maxScale - minScale);");
    client.println("      let y = plotHeight - (Math.max(0, Math.min(1, norm)) * (plotHeight - 10) + 5);");
    client.println("      ctx.fillStyle = '#dc3545'; ctx.beginPath(); ctx.arc(x, y, 3, 0, 2 * Math.PI); ctx.fill();");
    client.println("    }");
    client.println("  }");

    client.println("  ctx.fillStyle = '#888888'; ctx.font = '10px Arial';");
    client.println("  let now = new Date();");
    client.println("  let indices = [0, Math.floor((data.length - 1) / 2), data.length - 1];");
    client.println("  indices.forEach((idx, tIdx) => {");
    client.println("    let timeOffsetMs = (data.length - 1 - idx) * 2000;");
    client.println("    let t = new Date(now.getTime() - timeOffsetMs);");
    client.println("    let timeStr = t.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });");
    client.println("    let x = (idx / (150 - 1)) * c.width;");
    client.println("    ctx.textAlign = (tIdx === 0) ? 'left' : (tIdx === 1 ? 'center' : 'right');");
    client.println("    ctx.fillText(timeStr, x, c.height - 3);");
    client.println("  });");
    client.println("}");

    client.println("function updateDashboard() {");
    client.println("  fetch('/data').then(r => r.json()).then(d => {");
    client.println("    const b = document.getElementById('badge');");
    client.println("    b.innerText = d.status;");
    client.println("    b.style.background = d.goldi ? '#28a745' : (d.passed === 0 ? '#dc3545' : '#ffc107');");
    client.println("    b.style.color = (d.passed > 0 && !d.goldi) ? '#000000' : '#ffffff';");

    client.println("    const statusEl = document.getElementById('syncStatus');");
    client.println("    const btn = document.getElementById('syncBtn');");
    client.println("    if (d.synced) {");
    client.println("      statusEl.innerText = '\\u2713 Time Synced with Phone';");
    client.println("      statusEl.style.color = '#28a745';");
    client.println("      btn.style.display = 'none';");
    client.println("    } else if (statusEl.innerText !== 'Syncing phone time...') {");
    client.println("      statusEl.innerText = '\\u26A0 Time Unsynced';");
    client.println("      statusEl.style.color = '#dc3545';");
    client.println("      btn.style.display = 'inline-block';");
    client.println("    }");

    client.println("    const sEl = document.getElementById('salVal');");
    client.println("    sEl.innerText = d.salVal.toFixed(1) + ' ppt'; sEl.style.color = d.salOK ? '#0077b6' : '#dc3545';");

    client.println("    const tEl = document.getElementById('tempVal');");
    client.println("    tEl.innerText = d.tempVal.toFixed(1) + ' \\u00B0C'; tEl.style.color = d.tempOK ? '#00a896' : '#dc3545';");

    client.println("    const dEl = document.getElementById('depthVal');");
    client.println("    dEl.innerText = d.depthVal.toFixed(1) + ' cm'; dEl.style.color = d.depthOK ? '#7209b7' : '#dc3545';");

    client.println("    document.getElementById('logCount').innerText = d.samples;");

    client.println("    drawGraph('salChart', d.sal, '#0077b6', 0, 40, 15.0, 30.0);");
    client.println("    drawGraph('tempChart', d.temp, '#00a896', 0, 40, 10.0, 25.0);");
    client.println("    drawGraph('depthChart', d.depth, '#7209b7', 0, 150, 10.0, 80.0);");
    client.println("  }).catch(e => console.log(e));");
    client.println("}");

    client.println("updateDashboard(); setInterval(updateDashboard, 2000);");
    client.println("</script></body></html>");
  }

  delay(1);
  client.stop();
}
