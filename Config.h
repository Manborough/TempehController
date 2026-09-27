#pragma once

#include <cstdint>

#include "Secrets.h"
#include "src/control/Controller.h"

// build with DEMO_MODE=1 for the fast demo timings
#ifndef DEMO_MODE
#define DEMO_MODE 0
#endif

// every pin, address, timingm and threshold the firmware uses in one place
namespace Config {

constexpr bool kDemoMode = DEMO_MODE != 0;

// the Kasa smart plug that powers the heater
constexpr char kHeaterPlugMac[] = "B0:4E:26:C8:48:E0";
constexpr uint8_t kHeaterPlugIp[] = {192, 168, 0, 187};
constexpr uint32_t kHeaterConnectTimeoutMs = 1000;
constexpr uint32_t kHeaterReadTimeoutMs = 1000;

// ESP32 pins and the SHT31's I2C address
constexpr uint8_t kSubstratePin = 5;
constexpr uint8_t kFanPin = 21;
constexpr uint8_t kLedPin = 19;
constexpr uint8_t kI2cSdaPin = 22;
constexpr uint8_t kI2cSclPin = 23;
constexpr uint8_t kSht31Address = 0x45;

// how often the main loop runs and how often the cloud is updated
constexpr unsigned long kLoopDelayMs = kDemoMode ? 250UL : 2000UL;
constexpr unsigned long kCloudIntervalMs = kDemoMode ? 25000UL : 60000UL;

// how often the controller samples, checks, and retries the heater plug
constexpr ControllerTiming kControllerTiming{
    kDemoMode ? 1000UL : 60000UL,              // evidence sample interval
    kDemoMode ? 15000UL : 120000UL,            // evidence gap tolerance
    kDemoMode ? 15000UL : 60000UL,             // watchdog refresh interval
    kDemoMode ? 60U : 300U,                    // plug watchdog seconds
    kDemoMode ? 5000UL : 60000UL,              // state verification interval
    kDemoMode ? 5000UL : 5UL * 60UL * 1000UL,  // minimum relay dwell
    10000UL};                                   // failed plug command retry

// temperature (C) and humidity (%RH) thresholds for the control rules
constexpr ControlLimits kControlLimits{
    30.5f,   // heater on at/below
    31.5f,   // heater off at/above
    32.5f,   // temperature venting on at/above
    31.5f,   // venting off at/below (temperature and humidity)
    85.0f,   // humidity venting on above
    80.0f,   // humidity venting off at/below
    32.0f,   // humidity venting starts only at/above this substrate temp
    35.0f,   // overheat at/above (substrate or chamber)
    33.0f};  // overheat clears at/below (substrate and chamber)

// the requirements for the fermentation model
constexpr FermentationCriteria kFermentationCriteria{
    90,                      // ACTIVE: continuous heater off minutes
    kDemoMode ? 120 : 360,   // READY_CHECK: continuous heater off minutes
    1.5f,                    // minimum substrate/chamber self heating delta
    15,                      // ACTIVE: continuous self heating minutes
    kDemoMode ? 90 : 360};   // READY_CHECK: continuous self heating minutes

// all the controller settings together
constexpr ControllerConfig kController{
    kControllerTiming, kControlLimits, kFermentationCriteria};

}  // namespace Config
