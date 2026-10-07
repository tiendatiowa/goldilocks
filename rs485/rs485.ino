// Full RS485 Modbus Address & Baud Rate Scanner
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

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println("\n==========================================");
  Serial.println("  RS485 MODBUS MULTI-ADDRESS SCANNER      ");
  Serial.println("==========================================");

  delay(2000); // Allow probe MCU to stabilize

  scanBus(4800);
  scanBus(9600);
}

void scanBus(long baud) {
  Serial.print("\n>>> SCANNING AT "); Serial.print(baud); Serial.println(" BAUD <<<");
  Serial1.begin(baud);

  for (uint8_t addr = 1; addr <= 10; addr++) {
    while (Serial1.available()) Serial1.read(); // Clear buffer

    // Read 3 registers starting at 0x0000
    uint8_t req[8] = {addr, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
    uint16_t crc = calculateCRC16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    Serial1.write(req, 8);

    unsigned long start = millis();
    while (Serial1.available() < 11 && millis() - start < 150) {
      delay(2);
    }

    if (Serial1.available() >= 11) {
      uint8_t resp[11];
      Serial1.readBytes(resp, 11);

      if (resp[0] == addr && resp[1] == 0x03) {
        Serial.print("SUCCESS! Found RS485 Sensor at Address 0x");
        if (addr < 10) Serial.print("0");
        Serial.print(addr, HEX);
        Serial.print(" | Baud: "); Serial.println(baud);
        
        uint16_t ec = (resp[3] << 8) | resp[4];
        Serial.print("  -> Live Conductivity: "); Serial.print(ec); Serial.println(" uS/cm");
        return;
      }
    }
    Serial.print(".");
  }
  Serial.println("\nNo response on addresses 1-10.");
}

void loop() {}

