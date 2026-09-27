#pragma once

// What the  batch is doing now. The status LED, the cloud feed, and the serial status display this.
enum class BatchState {
  ESTABLISHING,
  ACTIVE,
  READY_CHECK,
  OVERHEAT,
  SENSOR_ERROR,
  HEATER_FAULT
};

// the name used on the serial status and cloud feeds
inline const char* batchStateName(BatchState state) {
  switch (state) {
    case BatchState::ESTABLISHING: return "ESTABLISHING";
    case BatchState::ACTIVE: return "ACTIVE";
    case BatchState::READY_CHECK: return "READY_CHECK";
    case BatchState::OVERHEAT: return "OVERHEAT";
    case BatchState::SENSOR_ERROR: return "SENSOR_ERROR";
    case BatchState::HEATER_FAULT: return "HEATER_FAULT";
  }
  return "UNKNOWN";
}
