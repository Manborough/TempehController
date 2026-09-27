#pragma once

#include <Adafruit_SHT31.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>

#include "ControllerIO.h"

// the DS18B20 probe in the beans and the SHT31 in the chamber
class Sensors {
 public:
  Sensors(uint8_t substratePin, uint8_t sdaPin, uint8_t sclPin,
          uint8_t chamberAddress);

  void begin();
  EnvironmentReading read(unsigned long nowMs);
  // whether the SHT31 is currently responding
  bool chamberReady() const { return chamberReady_; }
  void scanI2c(Stream& output);

 private:
  uint8_t sdaPin_;
  uint8_t sclPin_;
  uint8_t chamberAddress_;
  OneWire oneWire_;
  DallasTemperature substrateSensor_;
  Adafruit_SHT31 chamberSensor_;
  bool chamberReady_ = false;
  unsigned long lastRetryMs_ = 0;
};
