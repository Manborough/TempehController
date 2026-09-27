#pragma once

#include "../control/Controller.h"

// prints the controllers status and events to the serial monitor
class StatusDisplay {
 public:
  void reportEvents(const ControllerSnapshot& snapshot);
  void print(const ControllerSnapshot& snapshot, bool simulatedInputs);

 private:
  HeaterState lastEventHeater_ = HeaterState::UNKNOWN;
  uint16_t lastReportedFaultTotal_ = 0;
  unsigned long lastPrintMs_ = 0;
  bool printed_ = false;
  BatchState lastState_ = BatchState::ESTABLISHING;
  HeaterState lastHeater_ = HeaterState::UNKNOWN;
  bool lastFan_ = false;
};
