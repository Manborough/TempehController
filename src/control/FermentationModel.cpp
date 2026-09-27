#include "FermentationModel.h"

// start a new batch back at ESTABLISHING
void FermentationModel::reset() {
  clearEvidence();
  stage_ = BatchState::ESTABLISHING;
}

// restart both counts but keep the stage reached so far
void FermentationModel::clearEvidence() {
  heaterOffMinutes_ = 0;
  selfHeatMinutes_ = 0;
}

// add one minute of evidence, then move on a stage if its criteria are met
void FermentationModel::recordSample(float substrateTemp, float chamberTemp, bool heaterOn) {
  // the largest value a uint16_t can hold. (it wraps to 0 otherwise)
  constexpr uint16_t kMaxMinutes = 65535;
  if (heaterOn) {
    // a heater off stretch just ended, so record how long it was
    if (heaterOffMinutes_ > 0) {
      lastOffStretchMinutes_ = heaterOffMinutes_;
      ++offStretchCount_;
    }
    // the heater's warmth says nothing about the mould so reset the counts while heater is on.
    heaterOffMinutes_ = 0;
    selfHeatMinutes_ = 0;
  } else {
    if (heaterOffMinutes_ < kMaxMinutes) ++heaterOffMinutes_;
    // self heating must last and one sample below the delta restarts the count
    if (substrateTemp - chamberTemp < criteria_.minimumSelfHeatDelta)
      selfHeatMinutes_ = 0;
    else if (selfHeatMinutes_ < kMaxMinutes)
      ++selfHeatMinutes_;
  }

  // stages only move forward
  if (stage_ == BatchState::ESTABLISHING &&
      heaterOffMinutes_ >= criteria_.activeHeaterOffMinutes &&
      selfHeatMinutes_ >= criteria_.activeSelfHeatMinutes)
    stage_ = BatchState::ACTIVE;
  else if (stage_ == BatchState::ACTIVE &&
           heaterOffMinutes_ >= criteria_.readyHeaterOffMinutes &&
           selfHeatMinutes_ >= criteria_.readySelfHeatMinutes)
    stage_ = BatchState::READY_CHECK;
}
