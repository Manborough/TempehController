#include "CloudReporter.h"

// runs on a separate FreeRTOS task so a slow network never holds up control
CloudReporter::CloudReporter(AdafruitIO_WiFi& io, unsigned long intervalMs)
    : io_(io),
      intervalMs_(intervalMs),
      substrateFeed_(io.feed("substrate-temp")),
      chamberFeed_(io.feed("chamber-temp")),
      humidityFeed_(io.feed("humidity")),
      deltaTFeed_(io.feed("delta-t")),
      heaterFeed_(io.feed("heater")),
      fanFeed_(io.feed("fan")),
      stateFeed_(io.feed("state")),
      offStretchFeed_(io.feed("heater-off-min")),
      faultFeed_(io.feed("fault")),
      reportFeed_(io.feed("report")) {}

// create the one slot queue and the cloud task.
bool CloudReporter::begin() {
    // create the queue sized to hold one sample
  queue_ = xQueueCreate(1, sizeof(CloudSample));
  if (!queue_) return false;


  // create the task for cloud uploading. Tell it to run taskEntry, name the task cloud, give it 8192 bytes of stack,
  // and run the taskEntry from this object, 1 is priority, nullptr can accept a handle to a task for later use, which we don't need.
  if (xTaskCreate(taskEntry, "cloud", 8192, this, 1, nullptr) != pdPASS)  { // check if this task created properly
      // clean up if it didn't.
    vQueueDelete(queue_);
    queue_ = nullptr;
    return false;
  }
  return true;
}

// submit a sample to the queue for publishing
void CloudReporter::submit(const ControllerSnapshot& snapshot, int rssi) {
  if (!queue_) return;
  const CloudSample sample{snapshot, rssi, millis() / 1000UL};
  xQueueOverwrite(queue_, &sample);
}

// FreeRTOS is written in C and can't call a C++ method, so we define a plain function for it (taskEntry is static)
// 1. We hand FreeRTOS a plain function (taskEntry) and a pointer to our object (this).
// 2. FreeRTOS starts the task by calling taskEntry(pointer).
// 3. taskEntry restores the pointer's type and calls run() on the object.
void CloudReporter::taskEntry(void* context) {
  // context is the CloudReporter passed to xTaskCreate, so cast it back and call run().
  static_cast<CloudReporter*>(context)->run();
}

// connect and publish the latest snapshot at most once per interval
void CloudReporter::run() {
  io_.connect();
  unsigned long lastSentMs = 0;
  while (true) {
    io_.run();
    if (io_.status() >= AIO_CONNECTED && millis() - lastSentMs >= intervalMs_) {
      CloudSample sample;
      if (xQueueReceive(queue_, &sample, 0) == pdTRUE) {
        lastSentMs = millis();
        publish(sample);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// save each value to its feed
void CloudReporter::publish(const CloudSample& sample) {
  const ControllerSnapshot& s = sample.controller;

  // The heater feed displays 1 or 0, but the heater state can be unknown (-1) we only want on or off
  const int heaterValue = s.heater == HeaterState::ON ? 1 : 0;
  // save the values to AdafruitIO
  if (s.validSensors) {
    substrateFeed_->save(s.reading.substrateTemp);
    chamberFeed_->save(s.reading.chamberTemp);
    humidityFeed_->save(s.reading.humidity);
    deltaTFeed_->save(s.reading.deltaT());
  }
  heaterFeed_->save(heaterValue);
  fanFeed_->save(s.fan ? 1 : 0);
  stateFeed_->save(String(batchStateName(s.state)));

  // publish runs on a timer but we only want to send a stretch's length once when a new one has ended.
  // offStretchCount goes up by 1 each time the heater turns back on after an off stretch
  const bool newStretchEnded = s.offStretchCount != publishedStretchCount_;
  if (newStretchEnded) {
    publishedStretchCount_ = s.offStretchCount;
    offStretchFeed_->save(s.lastOffStretchMinutes);
  }

  // send the fault code once when a new fault has happened otherwise send 0 so each fault shows as one entry.
  // faultTotal goes up by 1 each time the controller records a fault
  const bool newFault = s.faultTotal != publishedFaultTotal_;
  const int faultCode = newFault ? static_cast<int>(s.lastFault) : 0;
  publishedFaultTotal_ = s.faultTotal;
  faultFeed_->save(faultCode);

  // total characters in a report line. should be enough.
  char report[192];
  buildReport(sample, faultCode, report, sizeof(report));
  reportFeed_->save(String(report));
}


// pack one sample into a single line for the report feed:
void CloudReporter::buildReport(const CloudSample& sample, int faultCode, char* out, size_t length) {
  const ControllerSnapshot& s = sample.controller;
  int heaterValue = -1;  // -1 when the heater state is not known
  if (s.heater == HeaterState::ON) {
    heaterValue = 1;
  } else if (s.heater == HeaterState::OFF) {
    heaterValue = 0;
  }
  char substrate[12] = "", chamber[12] = "", humidity[12] = "", delta[12] = "";
  if (s.validSensors) {
    snprintf(substrate, sizeof(substrate), "%.2f", s.reading.substrateTemp);
    snprintf(chamber, sizeof(chamber), "%.2f", s.reading.chamberTemp);
    snprintf(humidity, sizeof(humidity), "%.1f", s.reading.humidity);
    snprintf(delta, sizeof(delta), "%.2f", s.reading.deltaT());
  }
  snprintf(out, length,
           "%lu|%s|%s|%s|%s|%d|%d|%s|%d|%d|%u",
           sample.uptimeSec, substrate, chamber, humidity, delta,
           heaterValue, s.fan ? 1 : 0, batchStateName(s.state),
           sample.rssi, faultCode, s.faultTotal);
}
