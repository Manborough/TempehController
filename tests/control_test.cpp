#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../src/control/Controller.h"

// Fake smart plug. The *OK flags make a command fail; the counters record
// how often each command was sent.
struct MockSwitch final : FailsafeSwitch {
  bool physicalOn = false, onOK = true, offOK = true, readOK = true;
  bool refreshOK = true;
  int onCalls = 0, offCalls = 0, readCalls = 0, refreshCalls = 0;

  bool on() override {
    ++onCalls;
    physicalOn = true;
    return onOK;
  }
  bool off() override {
    ++offCalls;
    if (offOK) physicalOn = false;
    return offOK;
  }
  bool readState(bool& value) override {
    ++readCalls;
    if (readOK) value = physicalOn;
    return readOK;
  }
  bool refreshWatchdog() override {
    ++refreshCalls;
    return refreshOK;
  }
};

// Fake fan that only remembers its last setting.
struct MockFan final : FanActuator {
  bool on = false;
  void set(bool value) override { on = value; }
  bool isOn() const override { return on; }
};

// test thresholds
FermentationCriteria fermentationCriteria() {
  return {90,     // ACTIVE: continuous heater off minutes
          120,    // READY_CHECK: continuous heater off minutes
          1.5f,   // minimum substrate/chamber self heating delta
          15,     // ACTIVE: continuous self-heating minutes
          60};    // READY_CHECK: continuous self-heating minutes
}

// Timing and limits
ControllerConfig controllerConfig(bool demo) {
  return {{demo ? 1000UL : 60000UL,     // evidence sample interval
           demo ? 15000UL : 120000UL,   // evidence gap tolerance
           demo ? 15000UL : 60000UL,    // watchdog refresh interval
           demo ? 60U : 300U,           // plug watchdog seconds
           demo ? 5000UL : 60000UL,     // state verification interval
           demo ? 5000UL : 300000UL,    // minimum relay dwell
           10000UL},                    // failed plug command retry
          {30.5f,   // heater on at/below
           31.5f,   // heater off at/above
           32.5f,   // temperature venting on at/above
           31.5f,   // venting off at/below (temperature and humidity)
           85.0f,   // humidity venting on above
           80.0f,   // humidity venting off at/below
           32.0f,   // humidity venting starts only at/above this substrate temp
           35.0f,   // overheat at/above (substrate or chamber)
           33.0f},  // overheat clears at/below (substrate and chamber)
          fermentationCriteria()};
}

// One controller wired to the mocks, with a fake clock in milliseconds.
struct Fixture {
  unsigned long now = 1000000;
  MockSwitch plug;
  MockFan fan;
  ControllerConfig config;
  Controller controller;

  explicit Fixture(bool demo = false)
      : config(controllerConfig(demo)), controller(plug, fan, config) {
    controller.begin(now);
  }

  static EnvironmentReading reading(float substrate, float chamber,
                                    float humidity) {
    return {substrate, chamber, humidity};
  }

  // Moves the clock forward, then runs one controller tick.
  ControllerSnapshot step(EnvironmentReading reading,
                          unsigned long advanceMs = 0) {
    now += advanceMs;
    return controller.tick(reading, now);
  }

  // The first tick switches the plug off, so its state is known.
  ControllerSnapshot confirmOff() { return step(reading(31, 30, 70)); }
  // Waits out the minimum relay dwell so the heater may switch on.
  void makeHeaterEligible() { now += config.timing.heaterMinDwellMs; }
};

// Fermentation model on its own. Each sample stands for one minute.
void testModel() {
  FermentationModel model(fermentationCriteria());
  // 90 heater-off minutes with self-heating reach ACTIVE, not 89.
  for (int i = 0; i < 89; ++i) model.recordSample(31, 28, false);
  assert(model.stage() == BatchState::ESTABLISHING);
  model.recordSample(31, 28, false);
  assert(model.stage() == BatchState::ACTIVE);

  // Heating or clearing evidence resets the count but keeps the stage.
  model.recordSample(31, 28, true);
  assert(model.stage() == BatchState::ACTIVE &&
         model.heaterOffMinutes() == 0);
  model.clearEvidence();
  assert(model.stage() == BatchState::ACTIVE &&
         model.heaterOffMinutes() == 0);

  // 120 more minutes reach READY_CHECK, which then stays latched.
  for (int i = 0; i < 119; ++i) model.recordSample(31.4f, 28, false);
  assert(model.stage() == BatchState::ACTIVE);
  model.recordSample(31.5f, 29, false);
  assert(model.stage() == BatchState::READY_CHECK);
  model.recordSample(20, 20, true);
  assert(model.stage() == BatchState::READY_CHECK);
  assert(std::string(batchStateName(model.stage())) == "READY_CHECK");

  // A small delta is not self-heating, so the heater being off is not enough.
  model.reset();
  for (int i = 0; i < 120; ++i) model.recordSample(32, 31, false);
  assert(model.stage() == BatchState::ESTABLISHING);

  // ACTIVE needs sustained self-heating to go on to READY_CHECK.
  model.reset();
  for (int i = 0; i < 90; ++i) model.recordSample(32, 29, false);
  assert(model.stage() == BatchState::ACTIVE);
  for (int i = 0; i < 60; ++i) model.recordSample(32, 31, false);
  assert(model.stage() == BatchState::ACTIVE);

  // A late delta must still be sustained, and a break restarts it.
  model.reset();
  for (int i = 0; i < 100; ++i) model.recordSample(32, 32, false);
  for (int i = 0; i < 14; ++i) model.recordSample(32, 30, false);
  assert(model.stage() == BatchState::ESTABLISHING &&
         model.selfHeatMinutes() == 14);
  model.recordSample(32, 31, false);
  assert(model.selfHeatMinutes() == 0);
  for (int i = 0; i < 14; ++i) model.recordSample(32, 30.5f, false);
  assert(model.stage() == BatchState::ESTABLISHING);
  model.recordSample(32, 30.5f, false);
  assert(model.stage() == BatchState::ACTIVE);
  for (int i = 0; i < 44; ++i) model.recordSample(32, 30, false);
  assert(model.stage() == BatchState::ACTIVE &&
         model.heaterOffMinutes() >= 120 && model.selfHeatMinutes() == 59);
  model.recordSample(32, 30, false);
  assert(model.stage() == BatchState::READY_CHECK);

  // Switching the heater on resets both counts.
  model.reset();
  for (int i = 0; i < 20; ++i) model.recordSample(32, 29, false);
  model.recordSample(32, 29, true);
  assert(model.selfHeatMinutes() == 0 && model.heaterOffMinutes() == 0);

  // A heater off stretch is reported once when the heater comes back on.
  assert(model.lastOffStretchMinutes() == 20);
  const uint16_t stretches = model.offStretchCount();
  model.recordSample(32, 29, true);
  assert(model.offStretchCount() == stretches);
  for (int i = 0; i < 7; ++i) model.recordSample(32, 29, false);
  model.recordSample(32, 29, true);
  assert(model.lastOffStretchMinutes() == 7 &&
         model.offStretchCount() == stretches + 1);
}

// Heater fan and overheat rules run with demo and real timings.
void testControl(bool demo) {
  // The fan starts on until the plug state is confirmed. The heater then switches on and off at its limits.
  Fixture f(demo);
  assert(f.fan.on);
  auto snapshot = f.confirmOff();
  assert(snapshot.heater == HeaterState::OFF);
  f.makeHeaterEligible();
  snapshot = f.step(Fixture::reading(29, 29, 70));
  assert(f.plug.physicalOn && snapshot.heater == HeaterState::ON);
  snapshot = f.step(Fixture::reading(31.5, 30, 70));
  assert(!f.plug.physicalOn && snapshot.heater == HeaterState::OFF);

  // Overheat forces heater off and fan on and clears at the reset limit.
  Fixture hot(demo);
  hot.confirmOff();
  hot.makeHeaterEligible();
  hot.step(Fixture::reading(29, 29, 70));
  snapshot = hot.step(Fixture::reading(35.5, 30, 70));
  assert(snapshot.state == BatchState::OVERHEAT &&
         snapshot.heater == HeaterState::OFF && hot.fan.on);
  snapshot = hot.step(Fixture::reading(33, 33, 70));
  assert(snapshot.state != BatchState::OVERHEAT);

  // Overheat keeps the classifier sampling. Mould heat (substrate well above chamber)
  // keeps both counts; a hot room (small delta) resets self-heating.
  const unsigned long interval = demo ? 1000UL : 60000UL;
  Fixture mould(demo);
  mould.confirmOff();
  for (int i = 0; i < 20; ++i)
    snapshot = mould.step(Fixture::reading(32, 29, 70), interval);
  snapshot = mould.step(Fixture::reading(35.5, 30, 70), interval);
  assert(snapshot.state == BatchState::OVERHEAT && mould.fan.on &&
         !mould.plug.physicalOn);
  assert(snapshot.heaterOffMinutes == 21 && snapshot.selfHeatMinutes == 21);
  snapshot = mould.step(Fixture::reading(32, 29, 70), interval);
  assert(snapshot.heaterOffMinutes == 22 && snapshot.selfHeatMinutes == 22);

  Fixture room(demo);
  room.confirmOff();
  for (int i = 0; i < 20; ++i)
    snapshot = room.step(Fixture::reading(32, 29, 70), interval);
  snapshot = room.step(Fixture::reading(35.5, 35, 70), interval);
  assert(snapshot.state == BatchState::OVERHEAT);
  assert(snapshot.heaterOffMinutes == 21 && snapshot.selfHeatMinutes == 0);

  // Humidity venting runs from above 85 % down to 80 %.
  Fixture humidity(demo);
  humidity.confirmOff();
  humidity.step(Fixture::reading(32, 32, 86));
  assert(humidity.fan.on);
  humidity.step(Fixture::reading(32, 32, 83));
  assert(humidity.fan.on);
  humidity.step(Fixture::reading(31.5, 31.5, 80));
  assert(!humidity.fan.on);

  // Humidity venting starts only with warm beans, runs down to the vent off
  // temperature and does not restart in between.
  Fixture warmHumidity(demo);
  warmHumidity.confirmOff();
  warmHumidity.step(Fixture::reading(31.8, 29, 90));
  assert(!warmHumidity.fan.on);
  warmHumidity.step(Fixture::reading(32, 29, 90));
  assert(warmHumidity.fan.on);
  warmHumidity.step(Fixture::reading(31.6, 29, 90));
  assert(warmHumidity.fan.on);
  warmHumidity.step(Fixture::reading(31.5, 29, 90));
  assert(!warmHumidity.fan.on);
  warmHumidity.step(Fixture::reading(31.8, 29, 90));
  assert(!warmHumidity.fan.on);

  // Temperature venting runs from 32.5 C down to 31.5 C.
  Fixture temperature(demo);
  temperature.confirmOff();
  temperature.step(Fixture::reading(32.5, 32, 70));
  assert(temperature.fan.on);
  temperature.step(Fixture::reading(31.5, 31, 70));
  assert(!temperature.fan.on);

  // A finished batch is never reheated, even when cold. Resetting the model starts a new batch and allows heating again.
  Fixture selfHeat(demo);
  selfHeat.confirmOff();
  for (int i = 0; i < 120; ++i) {
    snapshot = selfHeat.step(Fixture::reading(32, 29, 70),
                             selfHeat.config.timing.evidenceIntervalMs);
  }
  assert(snapshot.state == BatchState::READY_CHECK);
  assert(!selfHeat.fan.on);
  const int onCalls = selfHeat.plug.onCalls;
  selfHeat.makeHeaterEligible();
  snapshot = selfHeat.step(Fixture::reading(29, 29, 70));
  assert(snapshot.heater == HeaterState::OFF &&
         selfHeat.plug.onCalls == onCalls);
  snapshot = selfHeat.step(Fixture::reading(35.5, 30, 70));
  assert(snapshot.state == BatchState::OVERHEAT && selfHeat.fan.on);
  selfHeat.step(Fixture::reading(29, 29, 70));
  selfHeat.controller.resetModel();
  snapshot = selfHeat.step(Fixture::reading(29, 29, 70));
  assert(snapshot.state == BatchState::ESTABLISHING &&
         snapshot.heater == HeaterState::ON);

  // Cold beans are not vented for humidity. The heater waits out its dwell.
  Fixture coldHumidity(demo);
  coldHumidity.confirmOff();
  snapshot = coldHumidity.step(Fixture::reading(29, 29, 95));
  assert(!snapshot.fan && snapshot.heater == HeaterState::OFF);
}

// NaN, infinity, the DS18B20 error values and out-of-range humidity are
// rejected as a sensor error.
void testValidation(bool demo) {
  for (const EnvironmentReading invalid : {
           Fixture::reading(NAN, 30, 85),
           Fixture::reading(-127, 30, 85),
           Fixture::reading(85, 30, 85),
           Fixture::reading(31, INFINITY, 85),
           Fixture::reading(31, 30, 101)}) {
    Fixture fixture(demo);
    const auto snapshot = fixture.step(invalid);
    assert(!snapshot.validSensors);
    assert(snapshot.state == BatchState::SENSOR_ERROR);
    assert(snapshot.lastFault == FaultCode::SENSOR);
  }
  Fixture valid(demo);
  assert(valid.step(Fixture::reading(31, 30, 85)).validSensors);
}

// Plug and sensor failures run with demo and real timings.
void testFailures(bool demo) {
  // A failed OFF is a fault and is retried after the retry interval.
  Fixture failedOff(demo);
  failedOff.plug.offOK = false;
  auto snapshot = failedOff.confirmOff();
  assert(snapshot.heater == HeaterState::FAULT);
  failedOff.plug.offOK = true;
  // not retried before the interval has passed
  const int offCallsAfterFailure = failedOff.plug.offCalls;
  snapshot = failedOff.step(Fixture::reading(29, 29, 70),
                            failedOff.config.timing.heaterRetryIntervalMs - 1);
  assert(snapshot.heater == HeaterState::FAULT &&
         failedOff.plug.offCalls == offCallsAfterFailure);
  snapshot = failedOff.step(Fixture::reading(29, 29, 70), 1);
  assert(snapshot.heater == HeaterState::OFF && !failedOff.plug.physicalOn);

  // A failed ON is a fault, and the heater is switched off at once.
  Fixture failedOn(demo);
  failedOn.confirmOff();
  failedOn.makeHeaterEligible();
  failedOn.plug.onOK = false;
  snapshot = failedOn.step(Fixture::reading(29, 29, 70));
  // the OFF is sent in the same tick as the failed ON, with no retry wait
  assert(failedOn.plug.onCalls == 1 && failedOn.plug.offCalls == 2);
  assert(snapshot.heater == HeaterState::OFF && !failedOn.plug.physicalOn);
  assert(snapshot.lastFault == FaultCode::HEATER_ON);

  // A failed watchdog refresh switches the heater off.
  Fixture watchdog(demo);
  watchdog.confirmOff();
  watchdog.makeHeaterEligible();
  watchdog.step(Fixture::reading(29, 29, 70));
  watchdog.plug.refreshOK = false;
  snapshot = watchdog.step(Fixture::reading(29, 29, 70),
                           watchdog.config.timing.watchdogRefreshIntervalMs);
  assert(!watchdog.plug.physicalOn && snapshot.heater == HeaterState::OFF);
  assert(snapshot.lastFault == FaultCode::WATCHDOG);

  // The plug turning on by itself is caught at the next verification. The plug is switched off and the evidence is cleared.
  Fixture mismatch(demo);
  mismatch.confirmOff();
  for (int i = 0; i < 10; ++i) {
    snapshot = mismatch.step(Fixture::reading(31, 24, 70),
                             mismatch.config.timing.evidenceIntervalMs);
  }
  assert(snapshot.heaterOffMinutes >= 10);
  mismatch.plug.physicalOn = true;
  snapshot = mismatch.step(Fixture::reading(31, 24, 70),
                           mismatch.config.timing.heaterVerifyIntervalMs);
  assert(!mismatch.plug.physicalOn);
  assert(snapshot.lastFault == FaultCode::HEATER_STATE);
  assert(snapshot.heaterOffMinutes <= 1);

  // A sensor error turns the fan on and counts once per episode.
  Fixture sensors(demo);
  snapshot = sensors.step(Fixture::reading(NAN, 30, 70));
  assert(!snapshot.validSensors && snapshot.fan);
  const uint16_t faultCount = snapshot.faultTotal;
  snapshot = sensors.step(Fixture::reading(NAN, 30, 70), 1);
  assert(snapshot.faultTotal == faultCount);

  const unsigned long interval = demo ? 1000UL : 60000UL;
  const unsigned long gap = demo ? 15000UL : 120000UL;
  // Evidence survives a short sensor glitch and a gap longer than the tolerance clears it.
  Fixture glitch(demo);
  glitch.confirmOff();
  for (int i = 0; i < 10; ++i)
    snapshot = glitch.step(Fixture::reading(32, 29, 70), interval);
  assert(snapshot.heaterOffMinutes == 10);
  snapshot = glitch.step(Fixture::reading(-127, 29, 70), interval);
  assert(snapshot.state == BatchState::SENSOR_ERROR &&
         !glitch.plug.physicalOn && glitch.fan.on);
  snapshot = glitch.step(Fixture::reading(32, 29, 70), interval);
  assert(snapshot.heaterOffMinutes == 11);

  snapshot = glitch.step(Fixture::reading(NAN, 29, 70), interval);
  snapshot = glitch.step(Fixture::reading(NAN, 29, 70), gap);
  snapshot = glitch.step(Fixture::reading(32, 29, 70), interval);
  assert(snapshot.heaterOffMinutes == 1);
}

int main() {
  testModel();
  for (bool demo : {false, true}) {
    testControl(demo);
    testValidation(demo);
    testFailures(demo);
  }
  std::puts("Controller and fermentation model regressions passed");
}
