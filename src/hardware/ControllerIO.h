#pragma once

// one reading from the sensors
struct EnvironmentReading {
  float substrateTemp = 0;
  float chamberTemp = 0;
  float humidity = 0;

  // how much warmer the beans are than the chamber
  float deltaT() const { return substrateTemp - chamberTemp; }
};

// A switch that turns itself off unless it is renewed. Used for the heater.
// This interface lets us swap the implementation between the real plug and during testing with whatever we want.
class FailsafeSwitch {
 public:
  virtual ~FailsafeSwitch() = default;

  // only succeeds if the output and its auto off were both confirmed
  virtual bool on() = 0;
  virtual bool off() = 0;

  // read the actual state into isOn
  virtual bool readState(bool& isOn) = 0;
  // restart the auto-off countdown
  virtual bool refreshWatchdog() = 0;
};

// the fan, as the controller sees it
class FanActuator {
 public:
  virtual ~FanActuator() = default;
  virtual void set(bool on) = 0;
  virtual bool isOn() const = 0;
};
