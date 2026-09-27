#include "StatusDisplay.h"

#include <Arduino.h>

// called every tick and reports events but only when they happen.
void StatusDisplay::reportEvents(const ControllerSnapshot& s) {
  if (s.heater != lastEventHeater_) {
    lastEventHeater_ = s.heater;
    if (s.heater == HeaterState::ON) Serial.println("HEATER ON");
    else if (s.heater == HeaterState::OFF) Serial.println("HEATER OFF confirmed");
  }

  if (s.faultTotal != lastReportedFaultTotal_) {
    lastReportedFaultTotal_ = s.faultTotal;
    Serial.print("FAULT: ");
    switch (s.lastFault) {
      case FaultCode::HEATER_OFF: Serial.println("heater OFF command failed"); break;
      case FaultCode::HEATER_ON: Serial.println("heater ON command failed"); break;
      case FaultCode::WATCHDOG:
        Serial.println("heater watchdog refresh failed"); break;
      case FaultCode::SENSOR: Serial.println("invalid sensor reading"); break;
      case FaultCode::HEATER_STATE: Serial.println("heater state mismatch"); break;
      case FaultCode::NONE: Serial.println("unspecified"); break;
    }
  }
}

namespace {

// short heater label for the status line
const char* heaterName(HeaterState state) {
  switch (state) {
    case HeaterState::ON: return "ON";
    case HeaterState::OFF: return "off";
    case HeaterState::FAULT: return "FLT";
    case HeaterState::UNKNOWN: return "??";
  }
  return "?";
}

const char* shortStateName(BatchState state) {
  switch (state) {
    case BatchState::ESTABLISHING: return "ESTAB";
    case BatchState::ACTIVE: return "ACTIVE";
    case BatchState::READY_CHECK: return "READY";
    case BatchState::OVERHEAT: return "OVERHEAT";
    case BatchState::SENSOR_ERROR: return "SENSOR";
    case BatchState::HEATER_FAULT: return "HTRFAULT";
  }
  return "?";
}

}  // namespace

// print the status lines every tick.
void StatusDisplay::print(const ControllerSnapshot& s, bool simulatedInputs) {
  const unsigned long nowMs = millis();
  const bool changed = !printed_ || s.state != lastState_ ||
                       s.heater != lastHeater_ || s.fan != lastFan_;
  if (!changed && nowMs - lastPrintMs_ < 1000UL) return;
  printed_ = true;
  lastPrintMs_ = nowMs;
  lastState_ = s.state;
  lastHeater_ = s.heater;
  lastFan_ = s.fan;

  char line[48];
  snprintf(line, sizeof(line), "sub %4.1f ch %4.1f rh %3.0f dT %+4.1f %3um",
           s.reading.substrateTemp, s.reading.chamberTemp, s.reading.humidity,
           s.reading.deltaT(), static_cast<unsigned>(s.selfHeatMinutes));
  Serial.println(line);

  snprintf(line, sizeof(line), "%-8s heat:%-3s fan:%-3s %3um%s",
           shortStateName(s.state), heaterName(s.heater), s.fan ? "ON" : "off",
           static_cast<unsigned>(s.heaterOffMinutes),
           simulatedInputs ? " SIM" : "");
  Serial.println(line);
}
