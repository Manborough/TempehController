#pragma once

#include <Adafruit_NeoPixel.h>

#include "ControllerIO.h"
#include "../control/BatchState.h"

// the heater interface.
FailsafeSwitch& heaterActuator();

// fan switched on and off by a GPIO pin
class HardwareFan final : public FanActuator {
 public:
  explicit HardwareFan(uint8_t pin) : pin_(pin) {}
  void begin();
  void set(bool on) override;
  bool isOn() const override { return on_; }

 private:
  uint8_t pin_;
  bool on_ = false;
};

// one NeoPixel that shows the batch state as a colour
class StatusLed {
 public:
  explicit StatusLed(uint8_t pin);
  void begin();
  void update(BatchState state);

 private:
  void set(uint8_t red, uint8_t green, uint8_t blue);
  Adafruit_NeoPixel led_;
};
