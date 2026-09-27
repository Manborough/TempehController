#pragma once

#include <cstdint>

#include "FermentationModel.h"
#include "../hardware/ControllerIO.h"

// how often the controller samples evidence and checks the heater plug
struct ControllerTiming {
  unsigned long evidenceIntervalMs;         // time between classifier samples
  unsigned long evidenceGapMs;              // longest sampling gap evidence survives
  unsigned long watchdogRefreshIntervalMs;  // how often the plug countdown is renewed
  uint32_t heaterWatchdogSeconds;           // plug turns itself off if not renewed
  unsigned long heaterVerifyIntervalMs;     // how often the plug state is read back
  unsigned long heaterMinDwellMs;           // shortest time between heater switches
  unsigned long heaterRetryIntervalMs;      // wait before retrying a failed command
};

// each rule switches on at one limit and off at another (hysteresis), so it does not flick on and off around a single value
struct ControlLimits {
  float heaterOnTemp;
  float heaterOffTemp;
  float ventOnTemp;
  float ventOffTemp;
  float ventOnHumidity;
  float ventOffHumidity;
  float humidityVentMinTemp;  // humidity venting starts only this warm
  float overheatTemp;         // substrate or chamber
  float overheatResetTemp;    // substrate and chamber
};

// all the controller settings filled in from Config.h
struct ControllerConfig {
  ControllerTiming timing;
  ControlLimits limits;
  FermentationCriteria fermentation;
};

// what went wrong; the number is what the cloud fault feed shows
enum class FaultCode {
  NONE = 0,
  HEATER_OFF = 1,    // OFF command failed
  HEATER_ON = 2,     // ON command failed
  WATCHDOG = 3,      // plug countdown could not be renewed
  SENSOR = 4,        // reading was missing or impossible
  HEATER_STATE = 5   // plug was not in the state we set
};

// what the controller believes the heater is doing. UNKNOWN until the first OFF succeeds.
// FAULT while the plug cannot be trusted.
enum class HeaterState {
  UNKNOWN = -1,
  OFF = 0,
  ON = 1,
  FAULT = 2
};

// the result of one tick used by the LED, cloud dashboard, ect to display state.
struct ControllerSnapshot {
  EnvironmentReading reading;
  bool validSensors;
  HeaterState heater;
  bool fan;
  BatchState state;
  // a fault is new when faultTotal has changed since the reader last looked
  FaultCode lastFault;
  uint16_t faultTotal;
  uint16_t heaterOffMinutes;
  uint16_t selfHeatMinutes;
  uint16_t lastOffStretchMinutes;
  uint16_t offStretchCount;
};

// runs the heater and fan rules and feeds the fermentation model
class Controller {
 public:
  Controller(FailsafeSwitch& heaterSwitch, FanActuator& fan, const ControllerConfig& config);

  void begin(unsigned long nowMs);
  ControllerSnapshot tick(const EnvironmentReading& reading, unsigned long nowMs);
  // start a new batch
  void resetModel() { model_.reset(); }

 private:
  bool validReadings(const EnvironmentReading& reading) const;
  void recordFault(FaultCode code);
  void markHeaterFault(FaultCode code);
  void heaterFaultShutdown(FaultCode code, unsigned long nowMs);
  void requestHeaterOff(unsigned long nowMs);
  void requestHeaterOn(unsigned long nowMs);
  void controlEnvironment(const EnvironmentReading& reading,
                          unsigned long nowMs);
  void serviceHeaterWatchdog(unsigned long nowMs);
  void verifyHeaterState(unsigned long nowMs);
  void handleSensorError(unsigned long nowMs);
  void sampleEvidence(const EnvironmentReading& reading, unsigned long nowMs);
  ControllerSnapshot snapshot(const EnvironmentReading& reading,
                              bool validSensors) const;

  bool heaterOn() const { return heaterState_ == HeaterState::ON; }

  // the plug state is known confirmed on or off.
  bool heaterOperational() const {
    return heaterState_ == HeaterState::OFF || heaterState_ == HeaterState::ON;
  }

  // after a failed heater command wait before trying again (don't spam the relay with requests)
  bool heaterRetryWaiting(unsigned long nowMs) const {
    return heaterRetryPending_ && nowMs - lastHeaterFailure_ < config_.timing.heaterRetryIntervalMs;
  }

  FailsafeSwitch& heaterSwitch_;
  FanActuator& fan_;
  ControllerConfig config_;
  FermentationModel model_;

  HeaterState heaterState_ = HeaterState::UNKNOWN;
  // rule states that stay on until their release limit
  bool overheatLatched_ = false;
  bool sensorFaultActive_ = false;
  bool humidityVent_ = false;
  bool temperatureVent_ = false;
  // the last heater command failed
  bool heaterRetryPending_ = false;

  // when each timed action last happened
  unsigned long lastWatchdogRefresh_ = 0;
  unsigned long lastHeaterVerify_ = 0;
  unsigned long lastHeaterSwitch_ = 0;
  unsigned long lastHeaterFailure_ = 0;
  unsigned long lastValidSample_ = 0;

  // most recent fault and how many there have been
  FaultCode lastFault_ = FaultCode::NONE;
  uint16_t faultTotal_ = 0;
};
