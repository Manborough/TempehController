#include "Actuators.h"

#include <Arduino.h>

#include "../../Config.h"

#include <KasaPlug.h>

namespace {

// Where the kasa library interfaces with the controller.
// This keeps Kasa out of the controller by wrapping it in an interface that can be swapped out during tests.
class KasaFailsafeSwitch final : public FailsafeSwitch {
 public:
  // set the plugs address, timeouts, and auto-off countdown from Config.h
  KasaFailsafeSwitch() {
    plug_.setWatchdogSeconds(Config::kController.timing.heaterWatchdogSeconds);
    plug_.setHost(IPAddress(Config::kHeaterPlugIp[0], Config::kHeaterPlugIp[1], Config::kHeaterPlugIp[2], Config::kHeaterPlugIp[3]));
    plug_.setTimeout(Config::kHeaterConnectTimeoutMs, Config::kHeaterReadTimeoutMs);
  }

  // fill in the methods required by the FailsafeSwitch interface
  bool on() override { return plug_.on(); }
  bool off() override { return plug_.off(); }
  bool readState(bool& isOn) override { return plug_.readState(isOn); }
  bool refreshWatchdog() override { return plug_.refreshWatchdog(); }

 private:
  KasaPlug plug_{Config::kHeaterPlugMac};
};

}  // namespace

// created once on first use with the plug set up from Config.h
FailsafeSwitch& heaterActuator() {
  static KasaFailsafeSwitch heater;
  return heater;
}

// set up the pin with the fan off
void HardwareFan::begin() {
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

// switch the fan and remember its state
void HardwareFan::set(bool on) {
  on_ = on;
  digitalWrite(pin_, on ? HIGH : LOW);
}

StatusLed::StatusLed(uint8_t pin) : led_(1, pin, NEO_GRB + NEO_KHZ800) {}

// start with the LED off
void StatusLed::begin() {
  led_.begin();
  led_.clear();
  led_.show();
}

// show one colour
void StatusLed::set(uint8_t red, uint8_t green, uint8_t blue) {
  led_.setPixelColor(0, led_.Color(red, green, blue));
  led_.show();
}

// blue while establishing, green when active, white when ready, red for any fault
void StatusLed::update(BatchState state) {
  switch (state) {
    case BatchState::ESTABLISHING: set(0, 0, 80); break;
    case BatchState::ACTIVE: set(0, 80, 0); break;
    case BatchState::READY_CHECK: set(60, 60, 60); break;
    case BatchState::SENSOR_ERROR:
    case BatchState::HEATER_FAULT:
    case BatchState::OVERHEAT: set(100, 0, 0); break;
  }
}
