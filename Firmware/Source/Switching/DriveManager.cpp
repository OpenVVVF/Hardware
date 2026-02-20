/**
***********************************************************************************
* @file    DriveManager.cpp
* @date    2026-02-19
* @brief   Implementation of the rate-decimated control pipeline.
***********************************************************************************
*/

#include "DriveManager.h"

void DriveManager::SetMotionController(MotionController* _motionController) {
    MotionController_ = _motionController;
}

void DriveManager::SetMotionUpdateRatio(uint16_t _ratio) {
    if (_ratio > 0) MotionUpdateRatio_ = _ratio;
}

void DriveManager::RegisterControlScheme(ControlScheme* _scheme) {
    ControlSelector_.RegisterScheme(_scheme);
}

void DriveManager::RegisterModulationScheme(ModulationScheme* _scheme) {
    ModulationSelector_.RegisterScheme(_scheme);
}

HardwareCommand DriveManager::Update(const SensorData& _Sensors, 
                                     const BaseMotionSetpoint& _Setpoint, 
                                     float _dt_S) {
    
    // 1. Accumulate physical time and increment the decimation counter
    AccumulatedMotionDt_S_ += _dt_S;
    MotionUpdateCounter_++;

    // 2. Outer Loop Execution (Low Speed)
    // Run the Motion Controller (e.g. Current/Torque loop) only on the Nth tick.
    if (MotionController_ && (MotionUpdateCounter_ >= MotionUpdateRatio_)) {
        
        // Pass the sum of all time steps since the last run for accurate physics
        CachedDriveCmd_ = MotionController_->Update(_Sensors, _Setpoint, AccumulatedMotionDt_S_);
        
        // Reset decimation trackers
        MotionUpdateCounter_ = 0;
        AccumulatedMotionDt_S_ = 0.0f;
    }

    // 3. Inner Loop Execution (High Speed)
    // The Control Selector and Modulation Selector always run at the full system rate.
    ModulationInput modInput = ControlSelector_.Update(_Sensors, CachedDriveCmd_, _dt_S);

    // 4. Final Modulation Output
    return ModulationSelector_.Update(modInput);
}