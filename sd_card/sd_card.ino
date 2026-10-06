#include <SPI.h>
#include "SdFat.h"

// Program to find out which PIN the DFR0229 will connect to
// Need SdFat library by Bill Greiman

SdFat sd;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println("\n--- Scanning CS Pins (2 to 10) ---");

  // Keep hardware SS pin as OUTPUT for Uno R4 SPI bus
  pinMode(10, OUTPUT);

  bool found = false;

  for (int csPin = 2; csPin <= 10; csPin++) {
    Serial.print("Testing CS Pin ");
    Serial.print(csPin);
    Serial.print("... ");

    if (sd.begin(csPin, SD_SCK_MHZ(1))) {
      Serial.println(" SUCCESS! SD Card found!");
      found = true;
      break;
    } else {
      Serial.println("No response.");
    }
    delay(100);
  }

  if (!found) {
    Serial.println("\n No SD card responded on Pins 2-10.");
    Serial.println("This indicates a physical contact issue on MISO, MOSI, or SCK.");
  }
}

void loop() {
  // Idle
}

