#pragma once

#include <cstdint>

#include "../hardware/ControllerIO.h"

// serial commands that fake sensor values and skip time during the demo
class DemoConsole {
 public:
  explicit DemoConsole(bool enabled) : enabled_(enabled) {}

  void begin();
  void poll();
  EnvironmentReading apply(EnvironmentReading reading) const;
  bool consumeResetRequest();
  uint16_t consumeFastForward();
  bool hasActiveInputs() const;

 private:
  void printHelp() const;

  bool enabled_;
  bool overrideSubstrate_ = false;
  bool overrideChamber_ = false;
  bool overrideHumidity_ = false;
  bool simulateSensorFailure_ = false;
  bool resetRequested_ = false;
  uint16_t fastForwardMinutes_ = 0;
  // values set by the s, c and u commands
  float substrateValue_ = 0;
  float chamberValue_ = 0;
  float humidityValue_ = 0;
};
