#include "Config.h"

#include <AdafruitIO_WiFi.h>
#include <WiFi.h>

#include "src/control/Controller.h"
#include "src/demo/DemoConsole.h"
#include "src/hardware/Actuators.h"
#include "src/hardware/Sensors.h"
#include "src/reporting/CloudReporter.h"
#include "src/reporting/StatusDisplay.h"

namespace {

// the hardware, controller and outputs created once at start-up
FailsafeSwitch& heaterSwitch = heaterActuator();

AdafruitIO_WiFi io(Secrets::kIoUsername, Secrets::kIoKey,
                   Secrets::kWifiSsid, Secrets::kWifiPassword);
HardwareFan fan(Config::kFanPin);
Controller controller(heaterSwitch, fan, Config::kController);
Sensors sensors(Config::kSubstratePin, Config::kI2cSdaPin,
                Config::kI2cSclPin, Config::kSht31Address);
CloudReporter cloud(io, Config::kCloudIntervalMs);
StatusDisplay statusDisplay;
StatusLed statusLed(Config::kLedPin);
DemoConsole demoConsole(Config::kDemoMode);
// in demo mode  fast forward moves the controller's clock ahead of millis()
unsigned long clockOffsetMs = 0;
unsigned long lastTickMs = 0;

// the time the controller sees including any fast forward
unsigned long controllerNowMs() { return millis() + clockOffsetMs; }

// run the controller evidence intervals forward one interval at a time.
void fastForward(const EnvironmentReading& reading, uint16_t minutes) {
  unsigned long stepMs = lastTickMs;
  for (uint16_t i = 0; i < minutes; ++i) {
    stepMs += Config::kControllerTiming.evidenceIntervalMs;
    statusDisplay.reportEvents(controller.tick(reading, stepMs));
  }
  const unsigned long offsetMs = stepMs - millis();
  if (static_cast<long>(offsetMs - clockOffsetMs) > 0) clockOffsetMs = offsetMs;
}

} // An anonymous namespace makes these helpers private to this file

// start the serial log, sensors, outputs, Wifi and the cloud task
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("================================");
  Serial.println("Tempeh controller booting");
  Serial.print("Build: DEMO_MODE="); Serial.println(Config::kDemoMode ? 1 : 0);
  Serial.println("================================");

  fan.begin();
  statusLed.begin();
  sensors.begin();
  // in demo mode list what is on the I2C bus to help check the wiring
  if (Config::kDemoMode) sensors.scanI2c(Serial);
  if (!sensors.chamberReady()) Serial.println("SHT31 unavailable. retrying connection");

  // WiFi connects in the background and the chamber control does not wait for it
  // ESP32 joins the home network, other things don't connect to it. (what STA is for. AP == access point for things joining the esp.)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(Secrets::kWifiSsid, Secrets::kWifiPassword);

  lastTickMs = millis();
  controller.begin(lastTickMs);
  if (!cloud.begin()) Serial.println("Cloud task unavailable. local control continues");
  demoConsole.begin();

  Serial.println("Tempeh controller started");
}

// read the sensors run one controller tick then show and send the result
void loop() {
  // apply any demo commands typed on the serial console
  demoConsole.poll();
  if (demoConsole.consumeResetRequest()) controller.resetModel();

  // what are the sensor readings?
  const EnvironmentReading reading = demoConsole.apply(sensors.read(controllerNowMs()));
  const uint16_t minutes = demoConsole.consumeFastForward();
  if (minutes > 0) fastForward(reading, minutes);

  const unsigned long nowMs = controllerNowMs();
  // keep track of the last tick in time for fast forward.
  lastTickMs = nowMs;

  // tick the controller and get the snapshot
  const ControllerSnapshot snapshot = controller.tick(reading, nowMs);
  statusLed.update(snapshot.state);
  statusDisplay.reportEvents(snapshot);
  cloud.submit(snapshot, WiFi.RSSI());

  // on a sensor error, skip the status lines and wait before reading again
  if (!snapshot.validSensors) {
    Serial.println("SENSOR ERROR");
    delay(2000);
    return;
  }

  statusDisplay.print(snapshot, demoConsole.hasActiveInputs());
  delay(Config::kLoopDelayMs);
}
