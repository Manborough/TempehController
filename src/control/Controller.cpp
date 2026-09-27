#include "Controller.h"

#include <cmath>

namespace {

// values the DS18B20 gives instead of a temperature; unplugged, and just powered on
constexpr float kDs18b20DisconnectedReading = -127.0f;
constexpr float kDs18b20PowerOnReading = 85.0f;

// switch on when start is true, off when stop is true, otherwise stay as it was
bool hysteresis(bool active, bool start, bool stop) {
  if (start) return true;
  if (stop) return false;
  return active;
}

}  // anon namespace

Controller::Controller(FailsafeSwitch& heaterSwitch, FanActuator& fan, const ControllerConfig& config)
    : heaterSwitch_(heaterSwitch),
    fan_(fan),
    config_(config),
    model_(config.fermentation) {}

void Controller::begin(unsigned long nowMs) {
  // start the timers at 0
  lastHeaterVerify_ = nowMs;
  lastValidSample_ = nowMs;
  fan_.set(true);
}

// check a reading is safe to act on. isfinite catches NaN and infinity, and the DS18B20 has its own fixed error values.
bool Controller::validReadings(const EnvironmentReading& r) const {
  return std::isfinite(r.substrateTemp) &&
         r.substrateTemp != kDs18b20DisconnectedReading &&
         r.substrateTemp != kDs18b20PowerOnReading &&
         std::isfinite(r.chamberTemp) &&
         std::isfinite(r.humidity) &&
         r.humidity >= 0.0f && r.humidity <= 100.0f;
}

// remember the latest fault and count it
void Controller::recordFault(FaultCode code) {
  lastFault_ = code;
  ++faultTotal_;
}

// stop trusting the plug until an OFF command works. The fault is counted once per episode and the evidence gathered so far is thrown away.

// the heater smart plug has faulted (not responsive usually)
void Controller::markHeaterFault(FaultCode code) {
    // the first time this function is called record the fault (prevents the fault count increasing more than once for a single fault.)
  if (heaterState_ != HeaterState::FAULT) recordFault(code);
  // set the heaterState to a fault.
  heaterState_ = HeaterState::FAULT;
  // don't accumulate evidence until the fault is recovered from.
  model_.clearEvidence();
}

// after a failed ON, watchdog renewal, or state check, try to switch the heater off straight away
void Controller::heaterFaultShutdown(FaultCode code, unsigned long nowMs) {
  markHeaterFault(code);
  heaterRetryPending_ = false;  // the OFF must not wait for the failed command
  requestHeaterOff(nowMs);
}

// switch the heater off unless it already is or the last failed try was too recent
void Controller::requestHeaterOff(unsigned long nowMs) {
  if (heaterState_ == HeaterState::OFF) return;
  if (heaterRetryWaiting(nowMs)) return;

  // send OFF and if it fails, mark a fault and wait before retrying
  if (!heaterSwitch_.off()) {
    markHeaterFault(FaultCode::HEATER_OFF);
    heaterRetryPending_ = true;
    lastHeaterFailure_ = nowMs;
    return;
  }
  // success: mark the heater as off and clear the retry flag
  heaterState_ = HeaterState::OFF;
  lastHeaterSwitch_ = nowMs;
  heaterRetryPending_ = false;
}

// switch the heater on but only from a confirmed OFF and once the minimum time since the last switch has passed
void Controller::requestHeaterOn(unsigned long nowMs) {
    // do nothing if it's already on
  if (heaterState_ != HeaterState::OFF) return;
  // only try every so often (don't stress the relay)
  if (nowMs - lastHeaterSwitch_ < config_.timing.heaterMinDwellMs ||
      heaterRetryWaiting(nowMs))
    return;
  // try to switch it on and report the fault if it fails
  if (!heaterSwitch_.on()) {
    heaterFaultShutdown(FaultCode::HEATER_ON, nowMs);
    return;
  }
  // sucess the heater is on. Reset the other flags.
  heaterState_ = HeaterState::ON;
  lastHeaterSwitch_ = nowMs;
  lastWatchdogRefresh_ = nowMs;
  heaterRetryPending_ = false;
}

// while heating, renew the plug's countdown so it does not switch itself off,If renewing fails, treat it as a heater fault.
void Controller::serviceHeaterWatchdog(unsigned long nowMs) {
  const ControllerTiming& timing = config_.timing;
  // if the heater is not on or the watchdog timing is 0 don't do anything.
  if (!heaterOn() || timing.heaterWatchdogSeconds == 0) return;
  // Don't try to refresh the watchdog too frequently.
  if (nowMs - lastWatchdogRefresh_ < timing.watchdogRefreshIntervalMs) return;
  // Update the refreshed time
  lastWatchdogRefresh_ = nowMs;
  // try to refresh and handle the failure if needed.
  if (!heaterSwitch_.refreshWatchdog())
    heaterFaultShutdown(FaultCode::WATCHDOG, nowMs);
}

// every so often read the plug back and treat a mismatch as a heater fault
void Controller::verifyHeaterState(unsigned long nowMs) {
  if (!heaterOperational()) return;
  if (nowMs - lastHeaterVerify_ < config_.timing.heaterVerifyIntervalMs) return;
  lastHeaterVerify_ = nowMs;
  bool actual = false;
  const bool readOk = heaterSwitch_.readState(actual);
  // plug failed to answer so try again ater
  if (!readOk) return;
  // plug agrees with the controller
  if (actual == heaterOn()) return;
  heaterFaultShutdown(FaultCode::HEATER_STATE, nowMs);
}

// apply the four automation rules in order: overheat, heating, temperature venting, and humidity venting
void Controller::controlEnvironment(const EnvironmentReading& r, unsigned long nowMs) {
  const ControlLimits& limits = config_.limits;
  const float substrate = r.substrateTemp;
  const float chamber = r.chamberTemp;

  // Rule 1: overheat protection. A plug that can't be verified, a hot room, or the substrate being excessively warm.
  // turn the fan on, the heater off, an display a RED led warning light.
  overheatLatched_ = hysteresis(
      overheatLatched_,
      substrate >= limits.overheatTemp || chamber >= limits.overheatTemp,
      substrate <= limits.overheatResetTemp && chamber <= limits.overheatResetTemp
  );
  if (overheatLatched_ || !heaterOperational()) {
    fan_.set(true);
    requestHeaterOff(nowMs);
    return;
  }

  // Rule 2: heat the beans. Once a batch reaches READY_CHECK it is never heated again, but a new batch (reset or reboot)
  // turns heating back on.
  const bool batchFinished = model_.stage() == BatchState::READY_CHECK;
  if (batchFinished || substrate >= limits.heaterOffTemp)
    requestHeaterOff(nowMs);
  else if (substrate <= limits.heaterOnTemp)
    requestHeaterOn(nowMs);

  // Rule 3: vent to cool the beans when the mould heats them too much.
  temperatureVent_ = hysteresis(
      temperatureVent_,
      substrate >= limits.ventOnTemp,
      substrate <= limits.ventOffTemp
  );

  // Rule 4: vent humid air but only while the beans are warm so the fan never cools them back down
  // to the heater threshold.
  humidityVent_ = hysteresis(
      humidityVent_,
      r.humidity > limits.ventOnHumidity && substrate >= limits.humidityVentMinTemp,
      r.humidity <= limits.ventOffHumidity || substrate <= limits.ventOffTemp
  );

  // the fan never runs while the heater is on. If a plug command above failed
  // the next tick switches to Rule 1
  fan_.set(!heaterOn() && (temperatureVent_ || humidityVent_));
}

// on a bad reading, go to the safe state (heater off, fan on) and count the fault.
void Controller::handleSensorError(unsigned long nowMs) {
  if (!sensorFaultActive_) recordFault(FaultCode::SENSOR);
  sensorFaultActive_ = true;
  fan_.set(true);
  requestHeaterOff(nowMs);
}

// give the classifier one sample per evidence interval, but only wheb tge heater state is known.
// A gap longer than evidenceGapMs throws the evidence away.
void Controller::sampleEvidence(const EnvironmentReading& reading, unsigned long nowMs) {
  const ControllerTiming& timing = config_.timing;
  if (!heaterOperational() ||
      nowMs - lastValidSample_ < timing.evidenceIntervalMs)
    return;
  if (nowMs - lastValidSample_ > timing.evidenceGapMs) model_.clearEvidence();
  lastValidSample_ = nowMs;
  model_.recordSample(reading.substrateTemp, reading.chamberTemp, heaterOn());
}

// run one control cycle.
ControllerSnapshot Controller::tick(const EnvironmentReading& reading, unsigned long nowMs) {
  // Check the reading and the plug
  if (!validReadings(reading)) {
    handleSensorError(nowMs);
    return snapshot(reading, false);
  }
  sensorFaultActive_ = false;

  // apply the rules
  verifyHeaterState(nowMs);
  controlEnvironment(reading, nowMs);
  // sample the state
  sampleEvidence(reading, nowMs);
  // renew the heater watchdog
  serviceHeaterWatchdog(nowMs);
  return snapshot(reading, true);
}

// packag eup the current state of the system for display
ControllerSnapshot Controller::snapshot(const EnvironmentReading& reading, bool validSensors) const {
  BatchState reportedState = model_.stage();
  if (!validSensors)
    reportedState = BatchState::SENSOR_ERROR;
  else if (!heaterOperational())
    reportedState = BatchState::HEATER_FAULT;
  else if (overheatLatched_)
    reportedState = BatchState::OVERHEAT;

  return {
      reading,
      validSensors,
      heaterState_,
      fan_.isOn(),
      reportedState,
      lastFault_,
      faultTotal_,
      model_.heaterOffMinutes(),
      model_.selfHeatMinutes(),
      model_.lastOffStretchMinutes(),
      model_.offStretchCount()};
}
