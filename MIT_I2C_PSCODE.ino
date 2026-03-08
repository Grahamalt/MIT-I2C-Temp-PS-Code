#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =========================
// I2C sensor addresses
// =========================
const uint8_t ADDRS[] = {0x49};

// =========================
// BLE UUIDs
// Reusing the UUID pattern from the earlier working pset
// =========================
#define SERVICE_UUID "7e02cb7a-8f3a-4e4a-94a8-12b9d1c9ea01"
#define TX_CHAR_UUID "7e02cb7a-8f3a-4e4a-94a8-12b9d1c9ea02"   // ESP32 -> Phone

BLEServer* g_server = nullptr;
BLECharacteristic* g_txChar = nullptr;
bool g_deviceConnected = false;

// =========================
// BLE server callbacks
// =========================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    g_deviceConnected = true;
    Serial.println("BLE client connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    g_deviceConnected = false;
    Serial.println("BLE client disconnected");

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->start();

    Serial.println("Advertising restarted");
  }
};

// =========================
// I2C helper functions
// =========================
bool i2cRead16(uint8_t a, uint8_t r, uint16_t &v) {
  Wire.beginTransmission(a);
  Wire.write(r);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)a, 2) != 2) return false;

  v = (Wire.read() << 8) | Wire.read();
  return true;
}

bool i2cWrite16(uint8_t a, uint8_t r, uint16_t v) {
  Wire.beginTransmission(a);
  Wire.write(r);
  Wire.write((uint8_t)(v >> 8));
  Wire.write((uint8_t)(v & 0xFF));
  return Wire.endTransmission() == 0;
}

// Config masks for MAX31875-like device
static const uint16_t RES_MASK  = 0x0060;
static const uint16_t SHDN_MASK = 0x0100;
static const uint16_t RATE_MASK = 0x0006;
static const uint16_t RATE_1SPS = 0x0002;

// Force 12-bit resolution, continuous conversion
bool force12_and_run(uint8_t a) {
  uint16_t cfg_before;
  if (!i2cRead16(a, 0x01, cfg_before)) return false;

  uint16_t cfg = cfg_before;
  cfg &= ~RES_MASK;
  cfg |= RES_MASK;               // set 12-bit
  cfg &= ~SHDN_MASK;             // continuous mode
  cfg = (cfg & ~RATE_MASK) | RATE_1SPS;

  if (!i2cWrite16(a, 0x01, cfg)) return false;

  delay(150);

  uint16_t cfg_after;
  if (!i2cRead16(a, 0x01, cfg_after)) return false;

  return (((cfg_after >> 5) & 0x3) == 0b11);
}

// Read 12-bit temperature
bool read12(uint8_t a, float &tC) {
  Wire.beginTransmission(a);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)a, 2) != 2) return false;

  uint16_t w = (Wire.read() << 8) | Wire.read();
  int16_t raw = ((int16_t)w) >> 4;

  // Sign extend 12-bit value
  if (raw & 0x0800) raw |= 0xF000;

  tC = raw * 0.0625f;
  return true;
}

// =========================
// Optional I2C scan helper
// =========================
void scanI2C() {
  Serial.println("Scanning I2C bus...");
  bool foundAny = false;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("Found I2C device at 0x");
      if (addr < 0x10) Serial.print("0");
      Serial.println(addr, HEX);
      foundAny = true;
    }
  }

  if (!foundAny) {
    Serial.println("No I2C devices found.");
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Booting ESP32 I2C Temp + BLE...");

  // Start I2C
  Wire.begin();

  // Scan for devices
  scanI2C();

  // Initialize known temp sensors
  for (uint8_t a : ADDRS) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("Configuring sensor at 0x");
      if (a < 0x10) Serial.print("0");
      Serial.print(a, HEX);

      if (force12_and_run(a)) {
        Serial.println(" -> set to 12-bit OK");
      } else {
        Serial.println(" -> FAILED to configure");
      }
    } else {
      Serial.print("Sensor not found at 0x");
      if (a < 0x10) Serial.print("0");
      Serial.println(a, HEX);
    }
  }

  // Start BLE
  BLEDevice::init("ESP32-TempFiber");

  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  BLEService* service = g_server->createService(SERVICE_UUID);

  g_txChar = service->createCharacteristic(
    TX_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_READ
  );

  g_txChar->addDescriptor(new BLE2902());
  g_txChar->setValue("BLE Ready");

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("BLE advertising started.");
  Serial.println("Open nRF Connect, connect, and enable notifications.");
}

// =========================
// Main loop
// =========================
void loop() {
  static uint32_t lastMs = 0;

  if (millis() - lastMs >= 1000) {
    lastMs = millis();

    String payload = "";

    for (uint8_t a : ADDRS) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() != 0) {
        continue;
      }

      float tC;
      if (read12(a, tC)) {
        payload += "Addr 0x";
        if (a < 0x10) payload += "0";
        payload += String(a, HEX);
        payload += ": ";
        payload += String(tC, 2);
        payload += " C";
      } else {
        payload += "Addr 0x";
        if (a < 0x10) payload += "0";
        payload += String(a, HEX);
        payload += ": read failed";
      }

      payload += "  |  ";
    }

    if (payload.length() == 0) {
      payload = "No sensor data";
    }

    Serial.println(payload);

    g_txChar->setValue(payload.c_str());

    if (g_deviceConnected) {
      g_txChar->notify();
      Serial.println("Notification sent");
    }
  }
}
