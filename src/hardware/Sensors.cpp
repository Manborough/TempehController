#include "Sensors.h"

// turns the raw sensor readings into an EnvironmentReading

#include <cmath>

Sensors::Sensors(uint8_t substratePin, uint8_t sdaPin, uint8_t sclPin, uint8_t chamberAddress)
    : sdaPin_(sdaPin),
      sclPin_(sclPin),
      chamberAddress_(chamberAddress),
      oneWire_(substratePin),
      substrateSensor_(&oneWire_),
      chamberSensor_(Adafruit_SHT31()) {}

// start both sensors and if the SHT31 is missing read() keeps retrying it
void Sensors::begin() {
  substrateSensor_.begin();
  Wire.begin(sdaPin_, sclPin_);
  chamberReady_ = chamberSensor_.begin(chamberAddress_);
}

// read both sensors
EnvironmentReading Sensors::read(unsigned long nowMs) {
  substrateSensor_.requestTemperatures();
  EnvironmentReading reading;
  reading.substrateTemp = substrateSensor_.getTempCByIndex(0);

  // the SHT31 has to be started with begin(), unlike the DS18B20 which is
  // searched for on every read. If it never connects or drops out, try
  // begin() again every 10 seconds and report NaN until it answers.
  if (!chamberReady_ && nowMs - lastRetryMs_ >= 10000UL) {
    lastRetryMs_ = nowMs;
    chamberReady_ = chamberSensor_.begin(chamberAddress_);
  }
  reading.chamberTemp = chamberReady_ ? chamberSensor_.readTemperature() : NAN;
  reading.humidity = chamberReady_ ? chamberSensor_.readHumidity() : NAN;
  if (!std::isfinite(reading.chamberTemp) || !std::isfinite(reading.humidity))
    chamberReady_ = false;
  return reading;
}

// used at boot to list all I2C addresses that answer to check the SHT31 is wired correctly.
// only used by the demo code now.
void Sensors::scanI2c(Stream& output) {
  output.println("Scanning I2C bus...");
  int found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      output.print("  device at 0x");
      output.println(address, HEX);
      ++found;
    }
  }
  if (found == 0) output.println("  nothing responded check wiring, pins, and power");
}
