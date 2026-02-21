/**
***********************************************************************************
* @file    DriveManager.cpp
* @date    2026-02-19
* @brief   Implementation of the rate-decimated control pipeline.
***********************************************************************************
*/

#include "DriveManager.h"
#include "Hardware.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

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

bool DriveManager::Update(FaultManager* _FaultManager, MotorConfig* _MotorConfig, PWMDriver* _Driver, 
                          const SensorData& _Sensors, const BaseMotionSetpoint& _Setpoint, float _dt_S) {
    
    // =========================================================================
    // Safe-State Bailout Helper
    // =========================================================================
    // Keeps the code tidy. Called whenever a fault is detected to immediately 
    // force the PWM slice to a safe state and return false.
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
    

    // A. Phase Current
    if (std::abs(_Sensors._Iu_A) > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Sens: Iu > HardMaxPhaseCurrent", FaultSeverity::Latched);
    if (std::abs(_Sensors._Iv_A) > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Sens: Iv > HardMaxPhaseCurrent", FaultSeverity::Latched);
    if (std::abs(_Sensors._Iw_A) > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Sens: Iw > HardMaxPhaseCurrent", FaultSeverity::Latched);

    // B. DC Bus Current (Motoring and Regen)
    if (_Sensors._Idc_A > _MotorConfig->_HardMaxDcBusCurrent_A) _FaultManager->ReportFault("Sens: Idc > HardMaxDcBusCurrent", FaultSeverity::Latched);
    if (_Sensors._Idc_A < -_MotorConfig->_HardMaxRegenCurrent_A) _FaultManager->ReportFault("Sens: Idc < -HardMaxRegenCrrnt", FaultSeverity::Latched);

    // C. DC Bus Voltage (Strict hardware minimum to prevent div-by-zero)
    if (_Sensors._DcBusVoltage_V <= 0.0f) _FaultManager->ReportFault("Sens: Vdc <= 0V", FaultSeverity::Latched);

    // D. Rotation Speed Check
    // rad/sec * 9.55 ~= rpm
    if (_Sensors._EncoderVelocity_RadPerSec * 9.55 > _MotorConfig->_HardMaxVelocity_RPM) _FaultManager->ReportFault("Sens: RPM > HardMaxVelocityRPM", FaultSeverity::Latched);
    if (_Sensors._EncoderVelocity_RadPerSec * 9.55 < _MotorConfig->_HardMinVelocity_RPM) _FaultManager->ReportFault("Sens: RPM < HardMinVelocityRPM", FaultSeverity::Latched);

    // #. Math/NaN Checks
    if (std::isnan(_Sensors._EncoderPosition_Rad) || std::isnan(_Sensors._EncoderVelocity_RadPerSec)) {
        _FaultManager->ReportFault("Sens: Encoder NaN", FaultSeverity::Latched);
    }

    // -> Tripwire
    _FaultManager->Update();
    if (_FaultManager->IsSystemFaulted()) return CommandSafeState();


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
        if (std::abs(CachedDriveCmd_._IqCmd_A) > _MotorConfig->_HardMaxPhaseCurrent_A) _FaultManager->ReportFault("Motion: Iq > MaxTorque", FaultSeverity::Latched);

        // Fundamental Electrical Frequency Limit (Using Hardware namespace)
        float electricalHz = (CachedDriveCmd_._VelocityCmd_RadPerSec * _MotorConfig->_PolePairs_unitless) / (2.0f * M_PI);
        if (electricalHz > Hardware::Limits::Fundamental::MAX_HZ) _FaultManager->ReportFault("Motion: Hz > FUND_MAX_HZ", FaultSeverity::Latched);
        if (electricalHz < Hardware::Limits::Fundamental::MIN_HZ) _FaultManager->ReportFault("Motion: Hz < FUND_MIN_HZ", FaultSeverity::Latched);

        // Vdq Feedforward Limit
        float maxVdqSq = (_Sensors._DcBusVoltage_V * _MotorConfig->_MaxModulation_unitless);
        maxVdqSq *= maxVdqSq;
        float vdqMagSq = (CachedDriveCmd_._VdFeedforward_V * CachedDriveCmd_._VdFeedforward_V) + (CachedDriveCmd_._VqFeedforward_V * CachedDriveCmd_._VqFeedforward_V);
        if (vdqMagSq > maxVdqSq) _FaultManager->ReportFault("Motion: Vdq FF > BusLimit", FaultSeverity::Latched);
    }

    // -> Tripwire
    if (_FaultManager->IsSystemFaulted()) return CommandSafeState();


    // =========================================================================
    // 2. Inner Loop Execution (High Speed / Modulation Input)
    // =========================================================================
    ModulationInput modInput = ControlSelector_.Update(_Sensors, CachedDriveCmd_, _dt_S);

    if (modInput.Vdc_V <= 0.0f) _FaultManager->ReportFault("Mod: Vdc <= 0V", FaultSeverity::Latched);
    
    float maxModVoltageSq = (modInput.Vdc_V * _MotorConfig->_MaxModulation_unitless);
    maxModVoltageSq *= maxModVoltageSq;
    float vAlphaBetaSq = (modInput.Valpha_V * modInput.Valpha_V) + (modInput.Vbeta_V * modInput.Vbeta_V);
    if (vAlphaBetaSq > maxModVoltageSq) _FaultManager->ReportFault("Mod: V_alpha/beta > BusLimit", FaultSeverity::Latched);

    if (std::isnan(modInput.Theta_Rad) || std::isnan(modInput.Valpha_V) || std::isnan(modInput.Vbeta_V)) {
        _FaultManager->ReportFault("Mod: Math Error (NaN)", FaultSeverity::Latched);
    }

    // -> Tripwire
    if (_FaultManager->IsSystemFaulted()) return CommandSafeState();


    // =========================================================================
    // 3. Final Modulation Output
    // =========================================================================
    HardwareCommand Output = ModulationSelector_.Update(modInput);

    // Switching Frequency Limits (Using Hardware namespace)
    if (Output.SwitchingFrequency_Hz > Hardware::Limits::Switching::MAX_HZ) _FaultManager->ReportFault("HwCmd: SwFreq > MAX_HZ", FaultSeverity::Latched);
    if (Output.SwitchingFrequency_Hz < Hardware::Limits::Switching::MIN_HZ) _FaultManager->ReportFault("HwCmd: SwFreq < MIN_HZ", FaultSeverity::Latched);

    // Physical Duty Cycle Limits
    if (Output.DutyPhU_unitless < 0.0f || Output.DutyPhU_unitless > 1.0f) _FaultManager->ReportFault("HwCmd: DutyPhU Out of Bounds", FaultSeverity::Latched);
    if (Output.DutyPhV_unitless < 0.0f || Output.DutyPhV_unitless > 1.0f) _FaultManager->ReportFault("HwCmd: DutyPhV Out of Bounds", FaultSeverity::Latched);
    if (Output.DutyPhW_unitless < 0.0f || Output.DutyPhW_unitless > 1.0f) _FaultManager->ReportFault("HwCmd: DutyPhW Out of Bounds", FaultSeverity::Latched);

    if (std::isnan(Output.DutyPhU_unitless) || std::isnan(Output.DutyPhV_unitless) || std::isnan(Output.DutyPhW_unitless)) {
        _FaultManager->ReportFault("HwCmd: Duty Cycle NaN", FaultSeverity::Latched);
    }

    // -> Final Tripwire
    if (_FaultManager->IsSystemFaulted()) return CommandSafeState();


    // =========================================================================
    // 4. HW Output Dispatch 
    // =========================================================================
    // If we made it here, the math is pristine and verified against physics.
    _Driver->setCarrierFrequency(Output.SwitchingFrequency_Hz);
    _Driver->setDutyCycles(Output.DutyPhU_unitless, 
                           Output.DutyPhV_unitless, 
                           Output.DutyPhW_unitless);

    return true;
}