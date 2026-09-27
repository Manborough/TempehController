#pragma once

#include <cstdint>

#include "BatchState.h"

// how long each kind of evidence must last to reach the next stage
struct FermentationCriteria {
  int activeHeaterOffMinutes;  // heater off this long to reach ACTIVE
  int readyHeaterOffMinutes;   // heater off this long to reach READY_CHECK
  float minimumSelfHeatDelta;  // beans this much warmer than the chamber
  int activeSelfHeatMinutes;   // minutes in a row at/above the delta for ACTIVE
  int readySelfHeatMinutes;    // minutes in a row at/above the delta for READY_CHECK
};

// works out the batch stage from one sample a minute of the temperatures and heater state
class FermentationModel {
 public:
  explicit FermentationModel(const FermentationCriteria& criteria)
      : criteria_(criteria) {}

  void reset();
  void clearEvidence();
  void recordSample(float substrateTemp, float chamberTemp, bool heaterOn);

  // ESTABLISHING, ACTIVE or READY_CHECK.
  BatchState stage() const { return stage_; }
  // how long the heater has been off
  uint16_t heaterOffMinutes() const { return heaterOffMinutes_; }
  // how long the beans have been self heating without a break
  uint16_t selfHeatMinutes() const { return selfHeatMinutes_; }
  // length of the last heater off stretch that ended with the heater coming back on.
  uint16_t lastOffStretchMinutes() const { return lastOffStretchMinutes_; }
  // goes up by one when a heater off stretch ends. Defined so the cloud reporter sends each stretch once
  uint16_t offStretchCount() const { return offStretchCount_; }

 private:
  FermentationCriteria criteria_;
  uint16_t heaterOffMinutes_ = 0;
  uint16_t selfHeatMinutes_ = 0;
  uint16_t lastOffStretchMinutes_ = 0;
  uint16_t offStretchCount_ = 0;
  BatchState stage_ = BatchState::ESTABLISHING;
};
