/**
***********************************************************************************
* @file    DriveManager.cpp
* @date    2026-02-19
* @brief   Implementation of the rate-decimated control pipeline.
***********************************************************************************
*/

#include "DriveManager.h"
#include "Hardware.h"

// 1 / (2 * PI) for fast Hz calculation
#define INV_TWO_PI 0.159154943f 
// 30 / PI for fast Rad/s to RPM conversion
#define RADS_TO_RPM 9.549296585f 

void DriveManager::SetMotionController(MotionController* _MotionController) {
    MotionController_ = _MotionController;
}

void DriveManager::SetMotionUpdateRatio(uint16_t _Ratio) {
    if (_Ratio > 0) MotionUpdateRatio_ = _Ratio;
}

void DriveManager::SetControlScheme(ControlScheme* _Scheme) {
    // If the scheme is actually changing, trigger a reset for bumpless transfer
    if (ActiveControlScheme_ != _Scheme) {
        ActiveControlScheme_ = _Scheme;
        if (ActiveControlScheme_) {
            ActiveControlScheme_->Reset(); 
        }
    }
}

void DriveManager::RegisterModulationScheme(ModulationScheme* _Scheme) {
    ModulationSelector_.RegisterScheme(_Scheme);
}

bool DriveManager::Update(FaultManager* _FaultManager, MotorConfig* _MotorConfig, PWMDriver* _Driver, 
                          const SensorData& _Sensors, const BaseMotionSetpoint& _Setpoint, float _dt_S) {
    
    // =========================================================================
    // Safe-State Bailout Helper
    // =========================================================================
    auto CommandSafeState = [&]() -> bool {
        if (_Driver) {
            _Driver->setCarrierFrequency(Hardware::Limits::Switching::MIN_HZ);
            _Driver->setDutyCycles(0.0f, 0.0f, 0.0f);
        }
        return false;
    };

    if (!_FaultManager || !_MotorConfig || !_Driver) {
        return CommandSafeState();
    }

    // =========================================================================
    // 0. Sensor Telemetry Sanity Checks
    // =========================================================================
    
    // Fast absolute values
    float absIu = (_Sensors._Iu_A < 0.0f) ? -_Sensors._Iu_A : _Sensors._Iu_A;
    float absIv = (_Sensors._Iv_A < 0.0f) ? -_Sensors._Iv_A : _Sensors._Iv_A;
    float absIw = (_Sensors._Iw_A < 0.0f) ? -_Sensors._Iw_A : _Sensors._Iw_A;

    // A. Phase Current
    if (absIu > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Sens: Iu > HardMaxPhaseCurrent", FaultSeverity::Latched);
    if (absIv > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Sens: Iv > HardMaxPhaseCurrent", FaultSeverity::Latched);
    if (absIw > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Sens: Iw > HardMaxPhaseCurrent", FaultSeverity::Latched);

    // B. DC Bus Current (Motoring and Regen)
    if (_Sensors._Idc_A > _MotorConfig->_HardMaxDcBusCurrent_A) _FaultManager->ReportFault("Sens: Idc > HardMaxDcBusCurrent", FaultSeverity::Latched);
    if (_Sensors._Idc_A < -_MotorConfig->_HardMaxRegenCurrent_A) _FaultManager->ReportFault("Sens: Idc < -HardMaxRegenCrrnt", FaultSeverity::Latched);

    // C. DC Bus Voltage
    if (_Sensors._DcBusVoltage_V <= 0.0f) _FaultManager->ReportFault("Sens: Vdc <= 0V", FaultSeverity::Latched);
    if (_Sensors._DcBusVoltage_V > 300.0f) _FaultManager->ReportFault("Sens: Vdc > 350V", FaultSeverity::Latched);

    // D. Fast Rotation Speed Check
    float rpm = _Sensors._EncoderVelocity_RadPerSec * RADS_TO_RPM;
    if (rpm > _MotorConfig->_HardMaxVelocity_RPM) _FaultManager->ReportFault("Sens: RPM > HardMaxVelocityRPM", FaultSeverity::Latched);
    if (rpm < _MotorConfig->_HardMinVelocity_RPM) _FaultManager->ReportFault("Sens: RPM < HardMinVelocityRPM", FaultSeverity::Latched);

    // // E. Math/NaN Checks using fast IEEE-754 (x != x) evaluation
    // if (_Sensors._EncoderPosition_Rad != _Sensors._EncoderPosition_Rad || 
    //     _Sensors._EncoderVelocity_RadPerSec != _Sensors._EncoderVelocity_RadPerSec) {
    //     _FaultManager->ReportFault("Sens: Encoder NaN", FaultSeverity::Latched);
    // }

    // =========================================================================
    // 1. Outer Loop Execution (Low Speed / Motion)
    // =========================================================================
    AccumulatedMotionDt_S_ += _dt_S;
    MotionUpdateCounter_++;

    if (MotionController_ && (MotionUpdateCounter_ >= MotionUpdateRatio_)) {
        
        CachedDriveCmd_ = MotionController_->Update(_Sensors, _Setpoint, AccumulatedMotionDt_S_);
        MotionUpdateCounter_ = 0;
        AccumulatedMotionDt_S_ = 0.0f;

        // Phase/Torque Current Limits
        float currentMagSq = (CachedDriveCmd_._IqCmd_A * CachedDriveCmd_._IqCmd_A) + (CachedDriveCmd_._IdCmd_A * CachedDriveCmd_._IdCmd_A);
        float maxCurrentSq = _MotorConfig->_HardMaxPhaseCurrent_A * _MotorConfig->_HardMaxPhaseCurrent_A;
        if (currentMagSq > maxCurrentSq) _FaultManager->ReportFault("Motion: IPhase > MaxPhase", FaultSeverity::Latched);
        
        float absIq = (CachedDriveCmd_._IqCmd_A < 0.0f) ? -CachedDriveCmd_._IqCmd_A : CachedDriveCmd_._IqCmd_A;
        if (absIq > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Motion: Iq > MaxTorque", FaultSeverity::Latched);

        // Fundamental Electrical Frequency Limit (Using fast multiplier instead of division)
        float electricalHz = (CachedDriveCmd_._VelocityCmd_RadPerSec * _MotorConfig->_PolePairs_unitless) * INV_TWO_PI;
        if (electricalHz > Hardware::Limits::Fundamental::MAX_HZ) _FaultManager->ReportFault("Motion: Hz > FUND_MAX_HZ", FaultSeverity::Latched);
        if (electricalHz < Hardware::Limits::Fundamental::MIN_HZ) _FaultManager->ReportFault("Motion: Hz < FUND_MIN_HZ", FaultSeverity::Latched);

        // Vdq Feedforward Limit
        float maxVdq = _Sensors._DcBusVoltage_V * _MotorConfig->_MaxModulation_unitless;
        float maxVdqSq = maxVdq * maxVdq;
        float vdqMagSq = (CachedDriveCmd_._VdFeedforward_V * CachedDriveCmd_._VdFeedforward_V) + (CachedDriveCmd_._VqFeedforward_V * CachedDriveCmd_._VqFeedforward_V);
        if (vdqMagSq > maxVdqSq) _FaultManager->ReportFault("Motion: Vdq FF > BusLimit", FaultSeverity::Latched);
    }

    // =========================================================================
    // 2. Inner Loop Execution (High Speed / Modulation Input)
    // =========================================================================
    ModulationInput modInput;
    
    if (ActiveControlScheme_) {
        modInput = ActiveControlScheme_->Update(_Sensors, CachedDriveCmd_, _dt_S);
    } else {
        modInput = {0.0f, 0.0f, _Sensors._DcBusVoltage_V, 0.0f, 0.0f}; 
    }

    // if (modInput.Vdc_V <= 0.0f) _FaultManager->ReportFault("Mod: Vdc <= 0V", FaultSeverity::Latched);
    
    // float maxModVoltage = modInput.Vdc_V * _MotorConfig->_MaxModulation_unitless;
    // float maxModVoltageSq = maxModVoltage * maxModVoltage;
    // float vAlphaBetaSq = (modInput.Valpha_V * modInput.Valpha_V) + (modInput.Vbeta_V * modInput.Vbeta_V);
    
    // if (vAlphaBetaSq > maxModVoltageSq) _FaultManager->ReportFault("Mod: V_alpha/beta > BusLimit", FaultSeverity::Latched);

    // if (modInput.Theta_Rad != modInput.Theta_Rad || 
    //     modInput.Valpha_V != modInput.Valpha_V || 
    //     modInput.Vbeta_V != modInput.Vbeta_V) {
    //     _FaultManager->ReportFault("Mod: Math Error (NaN)", FaultSeverity::Latched);
    // }

    // =========================================================================
    // 3. Final Modulation Output
    // =========================================================================
    HardwareCommand Output = ModulationSelector_.Update(modInput);

    // Switching Frequency Limits
    if (Output.SwitchingFrequency_Hz > Hardware::Limits::Switching::MAX_HZ) _FaultManager->ReportFault("HwCmd: SwFreq > MAX_HZ", FaultSeverity::Latched);
    if (Output.SwitchingFrequency_Hz < Hardware::Limits::Switching::MIN_HZ) _FaultManager->ReportFault("HwCmd: SwFreq < MIN_HZ", FaultSeverity::Latched);

    // // Physical Duty Cycle Limits
    // if (Output.DutyPhU_unitless < 0.0f || Output.DutyPhU_unitless > 1.0f) _FaultManager->ReportFault("HwCmd: DutyPhU Out of Bounds", FaultSeverity::Latched);
    // if (Output.DutyPhV_unitless < 0.0f || Output.DutyPhV_unitless > 1.0f) _FaultManager->ReportFault("HwCmd: DutyPhV Out of Bounds", FaultSeverity::Latched);
    // if (Output.DutyPhW_unitless < 0.0f || Output.DutyPhW_unitless > 1.0f) _FaultManager->ReportFault("HwCmd: DutyPhW Out of Bounds", FaultSeverity::Latched);

    // // NaN Duty Cycle Check
    // if (Output.DutyPhU_unitless != Output.DutyPhU_unitless || 
    //     Output.DutyPhV_unitless != Output.DutyPhV_unitless || 
    //     Output.DutyPhW_unitless != Output.DutyPhW_unitless) {
    //     _FaultManager->ReportFault("HwCmd: Duty Cycle NaN", FaultSeverity::Latched);
    // }

    // =========================================================================
    // 4. Single Point Fault Evaluation & HW Output Dispatch 
    // =========================================================================
    // Keep FaultManager speed cache updated for self-clearing faults
    _FaultManager->SetSpeed(_Sensors._EncoderVelocity_RadPerSec);
    _FaultManager->Update();

    // Final Tripwire Check - only branches/executes this single check per cycle!
    if (_FaultManager->IsSystemFaulted()) {
        return CommandSafeState();
    }

    _Driver->SetHardwareCommand(Output);

    return true;
}