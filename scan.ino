#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin();
  Serial.println("\n--- I2C Address Scanner ---");
}

void loop() {
  int deviceCount = 0;

  Serial.println("\nScanning...");

  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();

    if (error == 0) {
      deviceCount++;
      Serial.print("Device found at address 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
    }
  }

  if (deviceCount == 0) {
    Serial.println("No I2C devices found! Check wiring.");
  } else {
    Serial.print("Found ");
    Serial.print(deviceCount);
    Serial.println(" device(s).");
  }

  delay(5000);
}
