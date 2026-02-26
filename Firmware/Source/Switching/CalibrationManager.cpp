/**
***********************************************************************************
* @file    DriveManager.cpp
* @date    2026-02-19
* @brief   Implementation of the rate-decimated control pipeline.
***********************************************************************************
*/

#include "CalibrationManager.h"
#include "Hardware.h"


bool CalibrationManager::Update(FaultManager* _FaultManager, PWMDriver* _Driver, const SensorData& _Sensors, float _DT) {

    

    // Several modes

    switch (m_mode) {

        // -- ENCODER OFFSET -- //
        // encoder offset calibration (lock rotor to U phase, measure encoder has stopped changing, 
        // then sample value, perhaps then move rotor and lock to u phase again, maybe wrap to electrical and wrap, across all?)
        case CalibrationMode::ENCODER_OFFSET: {
    
            float dt = _DT; 
        
            switch (m_encoderCalib.CurrentState) {
                
                case EncoderCalibrationContext::State::INIT: {
                    // Configure PID limits - output is duty cycle modulation (+/- 0.15 is 15% max modulation)
                    m_encoderCalib.CurrentPid.Kp_ = 0.005f; // Needs tuning based on phase inductance/resistance
                    m_encoderCalib.CurrentPid.Ki_ = 0.001f;
                    m_encoderCalib.CurrentPid.MaxOutput_ = 0.15f; 
                    m_encoderCalib.CurrentPid.MinOutput_ = -0.15f;
                    m_encoderCalib.CurrentPid.Reset();
                    
                    m_encoderCalib.Timer_sec = 0.0f;
                    
                    _Driver->enable();
                    m_encoderCalib.CurrentState = EncoderCalibrationContext::State::WAIT_SETTLE;
                    break;
                }
        
                case EncoderCalibrationContext::State::WAIT_SETTLE: {
                    m_encoderCalib.Timer_sec += dt;
        
                    // Closed-loop control on Phase U current
                    float currentError = m_encoderCalib.TargetAlignCurrent_A - _Sensors._Iu_A;
                    
                    // Execute PID (0.0f feedforward)
                    float dutyModulation = m_encoderCalib.CurrentPid.Update(currentError, 0.0f, dt);
        
                    // Command Hardware - Push current into U, return split evenly through V and W
                    HardwareCommand cmd;
                    cmd.SwitchingFrequency_Hz = 4000.0f; 
                    cmd.DutyPhU_unitless = 0.5f + dutyModulation;
                    cmd.DutyPhV_unitless = 0.5f - (dutyModulation / 2.0f);
                    cmd.DutyPhW_unitless = 0.5f - (dutyModulation / 2.0f);
                    
                    _Driver->SetHardwareCommand(cmd);
        
                    // Check if rotor has settled
                    bool timeElapsed = (m_encoderCalib.Timer_sec >= m_encoderCalib.SettleTime_sec);
                    bool rotorStopped = (std::abs(_Sensors._EncoderVelocity_RadPerSec) < m_encoderCalib.VelocityThreshold);
        
                    if (timeElapsed && rotorStopped) {
                        m_encoderCalib.CurrentState = EncoderCalibrationContext::State::SAMPLE;
                    } else if (timeElapsed && !rotorStopped) {
                        // Safety catch: Rotor oscillating or struggling to align
                        _Driver->disable();
                        // _FaultManager->TriggerFault(Fault::CALIBRATION_TIMEOUT);
                    }
                    break;
                }
        
                case EncoderCalibrationContext::State::SAMPLE: {
                    m_encoderCalib.MeasuredOffset_Rad = _Sensors._EncoderPosition_Rad;
                    _Driver->disable(); // Cut power immediately after sampling
                    m_encoderCalib.CurrentState = EncoderCalibrationContext::State::DONE;
                    break;
                }
        
                case EncoderCalibrationContext::State::DONE: {
                    m_encoderCalib.CurrentState = EncoderCalibrationContext::State::INIT; 
                    m_mode = CalibrationMode::IDLE; 
                }
            }
            break;
        }



    // encoder min/max calibration (run motor open loop at a voltage until we see 2 full sin/cos mechanical revolutions, then find min/max, offsets)



    // lr calibration (resistance, flux+torque inductances, etc.)

    // flux linkage later


    }

    return false;
}