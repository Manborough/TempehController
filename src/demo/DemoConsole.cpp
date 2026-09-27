#include "DemoConsole.h"

// the demo commands only change the sensor values the controller sees

#include <DallasTemperature.h>

// print the demo banner and the command list
void DemoConsole::begin() {
  if (!enabled_) return;
  Serial.println("DEMO MODE: 1 evidence minute = 1 second");
  printHelp();
}

// list the commands
void DemoConsole::printHelp() const {
  Serial.println("--- demo commands ---");
  Serial.println("s <c>  override substrate temp");
  Serial.println("c <c>  override chamber temp");
  Serial.println("u <%>  override humidity");
  Serial.println("x      clear all overrides");
  Serial.println("f      toggle sensor failure");
  Serial.println("r      reset heater-off evidence and state");
  Serial.println("t <n>  fast-forward n evidence minutes");
  Serial.println("?      this help");
  Serial.println("---------------------");
}

// read one command line from serial and act on it
void DemoConsole::poll() {
  if (!enabled_ || !Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  const char command = line.charAt(0);
  const float value = line.substring(1).toFloat();
  switch (command) {
    case 's':
      overrideSubstrate_ = true; substrateValue_ = value;
      Serial.print("substrate forced to "); Serial.println(value); break;
    case 'c':
      overrideChamber_ = true; chamberValue_ = value;
      Serial.print("chamber forced to "); Serial.println(value); break;
    case 'u':
      overrideHumidity_ = true; humidityValue_ = value;
      Serial.print("humidity forced to "); Serial.println(value); break;
    case 'x':
      overrideSubstrate_ = overrideChamber_ = overrideHumidity_ = false;
      simulateSensorFailure_ = false;
      Serial.println("overrides cleared"); break;
    case 'f':
      simulateSensorFailure_ = !simulateSensorFailure_;
      Serial.print("simulated sensor failure ");
      Serial.println(simulateSensorFailure_ ? "ON" : "OFF"); break;
    case 'r':
      resetRequested_ = true;
      Serial.println("heater-off evidence and state reset requested"); break;
    case 't':
      fastForwardMinutes_ = value <= 0 ? 0 : value > 600 ? 600
                          : static_cast<uint16_t>(value);
      Serial.print("fast-forwarding "); Serial.print(fastForwardMinutes_);
      Serial.println(" minutes"); break;
    default: printHelp(); break;
  }
}

// true once after an r command
bool DemoConsole::consumeResetRequest() {
  const bool requested = resetRequested_;
  resetRequested_ = false;
  return requested;
}

// the minutes from the last t command, then zero
uint16_t DemoConsole::consumeFastForward() {
  const uint16_t minutes = fastForwardMinutes_;
  fastForwardMinutes_ = 0;
  return minutes;
}

// swap in any overridden values. A simulated failure uses the DS18B20's disconnected value
EnvironmentReading DemoConsole::apply(EnvironmentReading reading) const {
  if (!enabled_) return reading;
  if (overrideSubstrate_) reading.substrateTemp = substrateValue_;
  if (overrideChamber_) reading.chamberTemp = chamberValue_;
  if (overrideHumidity_) reading.humidity = humidityValue_;
  if (simulateSensorFailure_) reading.substrateTemp = DEVICE_DISCONNECTED_C;
  return reading;
}

// true if any override or simulated failure is on
bool DemoConsole::hasActiveInputs() const {
  return enabled_ && (overrideSubstrate_ || overrideChamber_ ||
                      overrideHumidity_ || simulateSensorFailure_);
}
