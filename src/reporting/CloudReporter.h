#pragma once

#include <AdafruitIO_WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../control/Controller.h"

// sends controller snapshots to AdafruitIO from a background task
class CloudReporter {
 public:
  CloudReporter(AdafruitIO_WiFi& io, unsigned long intervalMs);

  bool begin();
  // hand the latest snapshot to the cloud task replacing any unsent one. The cloud task publishes at most once per interval.
  // The interval is passed on construction. This is a way to manage the adafruit free tier which throttles on too many
  // publishes. It's set to 60 seconds currently.
  void submit(const ControllerSnapshot& snapshot, int rssi);

 private:
  // one snapshot plus the WiFi signal strength and uptime when it was taken
  struct CloudSample {
    ControllerSnapshot controller;
    int rssi;
    unsigned long uptimeSec;
  };

  static void taskEntry(void* context);
  void run();
  void publish(const CloudSample& sample);
  static void buildReport(const CloudSample& sample, int faultCode, char* out,
                          size_t length);

  AdafruitIO_WiFi& io_;
  unsigned long intervalMs_;
  QueueHandle_t queue_ = nullptr;
  // for the cloud task, the counts already published so each stretch and fault is sent once
  uint16_t publishedStretchCount_ = 0;
  uint16_t publishedFaultTotal_ = 0;

  // one Adafruit IO feed per value
  AdafruitIO_Feed* substrateFeed_;
  AdafruitIO_Feed* chamberFeed_;
  AdafruitIO_Feed* humidityFeed_;
  AdafruitIO_Feed* deltaTFeed_;
  AdafruitIO_Feed* heaterFeed_;
  AdafruitIO_Feed* fanFeed_;
  AdafruitIO_Feed* stateFeed_;
  AdafruitIO_Feed* offStretchFeed_;
  AdafruitIO_Feed* faultFeed_;
  AdafruitIO_Feed* reportFeed_;
};
