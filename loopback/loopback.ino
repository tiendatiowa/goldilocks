void setup() {
  Serial.begin(115200);
  Serial1.begin(4800);
  while (!Serial && millis() < 3000);
  
  Serial.println("Testing Serial1 Loopback...");
  Serial1.print("HELLO");
  delay(50);
  
  if (Serial1.available()) {
    String rx = Serial1.readString();
    Serial.print("✅ Loopback Passed! Received: ");
    Serial.println(rx);
  } else {
    Serial.println("❌ Loopback Failed. Check RUN/PROG switch position.");
  }
}

void loop() {}

