/**
***********************************************************************************
* @file    DriveManager.cpp
* @date    2026-02-19
* @brief   Implementation of the rate-decimated control pipeline.
***********************************************************************************
*/

#include "DriveManager.h"

void DriveManager::SetMotionController(MotionController* _MotionController) {
    MotionController_ = _MotionController;
}

void DriveManager::SetMotionUpdateRatio(uint16_t _Ratio) {
    if (_Ratio > 0) MotionUpdateRatio_ = _Ratio;
}

void DriveManager::RegisterControlScheme(ControlScheme* _Scheme) {
    ControlSelector_.RegisterScheme(_Scheme);
}

void DriveManager::RegisterModulationScheme(ModulationScheme* _Scheme) {
    ModulationSelector_.RegisterScheme(_Scheme);
}

HardwareCommand DriveManager::Update(FaultManager* _FaultManager, MotorConfig* _MotorConfig, const SensorData& _Sensors, 
                                     const BaseMotionSetpoint& _Setpoint, 
                                     float _dt_S) {
    if (!_FaultManager || !_MotorConfig) {
        return {0};
    }


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

        // -- Check for faults --
        
        // A. Phase Current Vector Limit (Id^2 + Iq^2 > Imax^2)
        float currentMagSq = (CachedDriveCmd_._IqCmd_A * CachedDriveCmd_._IqCmd_A) + 
                                (CachedDriveCmd_._IdCmd_A * CachedDriveCmd_._IdCmd_A);
        float maxCurrentSq = _MotorConfig->_MaxPhaseCurrent_A * _MotorConfig->_MaxPhaseCurrent_A;
        
        if (currentMagSq > maxCurrentSq) {
            _FaultManager->ReportFault("Motion: IPhase>_MaxPhaseCurrent_A", FaultSeverity::Latched);
        }

        // B. Torque Current Limit (Iq specific)
        if (fabs(CachedDriveCmd_._IqCmd_A) > _MotorConfig->_MaxTorqueCurrent_A) {
            _FaultManager->ReportFault("Motion: Iq Cmd > MaxTorque", FaultSeverity::Latched);
        }

        // C. Velocity Limit
        if (CachedDriveCmd_._VelocityCmd_RadPerSec > _MotorConfig->_MaxVelocity_RadPerSec) {
            _FaultManager->ReportFault("Motion: Vel Cmd > _MaxVelocity_RadPerSec", FaultSeverity::Latched);
        }
        if (CachedDriveCmd_._VelocityCmd_RadPerSec < _MotorConfig->_MinVelocity_RadPerSec) {
            _FaultManager->ReportFault("Motion: Vel Cmd < _MinVelocity_RadPerSec", FaultSeverity::Latched);
        }

        // D. Voltage Feedforward Limit (Vd^2 + Vq^2 > Vmax^2)
        float maxVdq = _MotorConfig->_DcBusVoltage_V * _MotorConfig->_MaxModulation_unitless;
        float vdqMagSq = (CachedDriveCmd_._VdFeedforward_V * CachedDriveCmd_._VdFeedforward_V) + 
                            (CachedDriveCmd_._VqFeedforward_V * CachedDriveCmd_._VqFeedforward_V);
                            
        if (vdqMagSq > (maxVdq * maxVdq)) {
            _FaultManager->ReportFault("Motion: Vdq FF > BusLimit", FaultSeverity::Latched);
        }

        _FaultManager->Update();
        
    }

    // 3. Inner Loop Execution (High Speed)
    // The Control Selector and Modulation Selector always run at the full system rate.
    ModulationInput modInput = ControlSelector_.Update(_Sensors, CachedDriveCmd_, _dt_S);

    // 4. Final Modulation Output
    return ModulationSelector_.Update(modInput);
}