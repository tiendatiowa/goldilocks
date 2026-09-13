/*
For DFRobot_EC, download directly from https://github.com/DFRobot/DFRobot_EC and Add to Arduino IDE:

1. Open the Arduino IDE on your Mac.
2. Go to Sketch > Include Library > Add .ZIP Library...
3. Select the downloaded file (DFRobot_EC-master.zip).

Then to make it work with Arduino UNO R4 Wifi, edit the file DFRobot_EC.cpp under ~/Documents/Arduino/libraries/DFRobot_EC-master

#include <ctype.h>

// ARM 32-bit compatibility fix for Arduino Uno R4
static char* strupr(char* str) {
    char* p = str;
    while (*p) {
        *p = toupper((unsigned char)*p);
        p++;
    }
    return str;
}

Save the chanage. You should be able to compile and upload this program.
*/
#include <DFRobot_EC.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define EC_PIN A0
#define ONE_WIRE_BUS 5 

float voltage, ecValue;
float temperature; // live temperature from the DS18B20 probe
DFRobot_EC ec;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

void setup() {
  Serial.begin(115200);
  ec.begin(); // Load saved calibration factors from EEPROM
  tempSensor.begin();
  Serial.println("--- DFRobot EC Sensor Calibration Mode (K=1) ---");
  Serial.println("Commands:");
  Serial.println("  enterec  -> Enter calibration mode");
  Serial.println("  calec    -> Calibrate against current buffer solution");
  Serial.println("  exitec   -> Save parameters to EEPROM & exit");
  Serial.println("------------------------------------------");
}

void loop() {
  static unsigned long lastSample = 0;
  if (millis() - lastSample > 1000) {
    lastSample = millis();
    
    // Read raw voltage in mV (5000 mV reference for 5V Arduino)
    voltage = analogRead(EC_PIN) / 1024.0 * 5000.0;

    tempSensor.requestTemperatures();
    temperature = tempSensor.getTempCByIndex(0);
    
    // Calculate conductivity in mS/cm
    ecValue = ec.readEC(voltage, temperature);

    Serial.print("Voltage: ");
    Serial.print(voltage, 1);
    Serial.print(" mV | Temp: ");
    Serial.print(temperature, 1);
    Serial.print(" C | EC: ");
    Serial.print(ecValue, 2);
    Serial.println(" mS/cm");
  }

  // Listen for serial commands ("enterec", "calec", "exitec")
  ec.calibration(voltage, temperature);
}

